////////////////////////////////////////////////////////////////////////////////
/// @brief V8 utility functions
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2011-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "v8-utils.h"

#include <fstream>
#include <locale>

#include <readline/readline.h>
#include <readline/history.h>

#include "Basics/StringUtils.h"
#include "BasicsC/conversions.h"
#include "BasicsC/csv.h"
#include "BasicsC/files.h"
#include "BasicsC/logging.h"
#include "BasicsC/string-buffer.h"
#include "BasicsC/strings.h"

#include "ShapedJson/shaped-json.h"

#include "V8/v8-json.h"

#if RL_READLINE_VERSION >= 0x0500
#define completion_matches rl_completion_matches
#endif

using namespace std;
using namespace triagens::basics;

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

static bool FillShapeValueJson (TRI_shaper_t* shaper,
                                TRI_shape_value_t* dst,
                                v8::Handle<v8::Value> json,
                                set<int>& seenHashes,
                                vector< v8::Handle<v8::Object> >& seenObjects);

static v8::Handle<v8::Value> JsonShapeData (TRI_shaper_t* shaper,
                                            TRI_shape_t const* shape,
                                            char const* data,
                                            size_t size);

// -----------------------------------------------------------------------------
// --SECTION--                                                class V8LineEditor
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Shell
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief word break characters
////////////////////////////////////////////////////////////////////////////////

static char WordBreakCharacters[] = {
    ' ', '\t', '\n', '"', '\\', '\'', '`', '@',
    '.', '>', '<', '=', ';', '|', '&', '{', '(',
    '\0'
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Shell
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief completion generator
////////////////////////////////////////////////////////////////////////////////

static char* CompletionGenerator (const char* text, int state) {
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief attempted completion
////////////////////////////////////////////////////////////////////////////////

static char** AttemptedCompletion (const char* text, int start, int end) {
  char** result = completion_matches(text, CompletionGenerator);
  rl_attempted_completion_over = true;
  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if javascript is missing a closing bracket
////////////////////////////////////////////////////////////////////////////////

static bool CheckJavaScript (string const& source) {
  char const* ptr;
  char const* end;
  int openParen;
  int openBrackets;
  int openBraces;

  enum {
    NORMAL,             // start
    NORMAL_1,           // from NORMAL: seen a single /
    DOUBLE_QUOTE,       // from NORMAL: seen a single "
    DOUBLE_QUOTE_ESC,   // from DOUBLE_QUOTE: seen a backslash
    SINGLE_QUOTE,       // from NORMAL: seen a single '
    SINGLE_QUOTE_ESC,   // from SINGLE_QUOTE: seen a backslash
    MULTI_COMMENT,      // from NORMAL_1: seen a *
    MULTI_COMMENT_1,    // from MULTI_COMMENT, seen a *
    SINGLE_COMMENT      // from NORMAL_1; seen a /
  }
  state;

  openParen = 0;
  openBrackets = 0;
  openBraces = 0;

  ptr = source.c_str();
  end = ptr + source.length();
  state = NORMAL;

  while (ptr < end) {
    if (state == DOUBLE_QUOTE) {
      if (*ptr == '\\') {
        state = DOUBLE_QUOTE_ESC;
      }
      else if (*ptr == '"') {
        state = NORMAL;
      }

      ++ptr;
    }
    else if (state == DOUBLE_QUOTE_ESC) {
      state = DOUBLE_QUOTE;
      ptr++;
    }
    else if (state == SINGLE_QUOTE) {
      if (*ptr == '\\') {
        state = SINGLE_QUOTE_ESC;
      }
      else if (*ptr == '\'') {
        state = NORMAL;
      }

      ++ptr;
    }
    else if (state == SINGLE_QUOTE_ESC) {
      state = SINGLE_QUOTE;
      ptr++;
    }
    else if (state == MULTI_COMMENT) {
      if (*ptr == '*') {
        state = MULTI_COMMENT_1;
      }

      ++ptr;
    }
    else if (state == MULTI_COMMENT_1) {
      if (*ptr == '/') {
        state = NORMAL;
      }

      ++ptr;
    }
    else if (state == SINGLE_COMMENT) {
      ++ptr;

      if (ptr == end) {
        state = NORMAL;
      }
    }
    else if (state == NORMAL_1) {
      switch (*ptr) {
        case '/':
          state = SINGLE_COMMENT;
          ++ptr;
          break;

        case '*':
          state = MULTI_COMMENT;
          ++ptr;
          break;

        default:
          state = NORMAL; // try again, do not change ptr
          break;
      }
    }
    else {
      switch (*ptr) {
        case '"':
          state = DOUBLE_QUOTE;
          break;

        case '\'':
          state = SINGLE_QUOTE;
          break;

        case '/':
          state = NORMAL_1;
          break;

        case '(':
          ++openParen;
          break;

        case ')':
          --openParen;
          break;

        case '[':
          ++openBrackets;
          break;

        case ']':
          --openBrackets;
          break;

        case '{':
          ++openBraces;
          break;

        case '}':
          --openBraces;
          break;
      }

      ++ptr;
    }
  }

  return openParen <= 0 && openBrackets <= 0 && openBraces <= 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                           static public variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Shell
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief history file
////////////////////////////////////////////////////////////////////////////////

const char* V8LineEditor::HISTORY_FILENAME = ".avocado";

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Shell
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new editor
////////////////////////////////////////////////////////////////////////////////

V8LineEditor::V8LineEditor ()
  : current() {
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Shell
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief line editor open
////////////////////////////////////////////////////////////////////////////////

bool V8LineEditor::open () {
  rl_initialize();
  rl_attempted_completion_function = AttemptedCompletion;
  rl_completer_word_break_characters = WordBreakCharacters;

  rl_bind_key('\t', rl_complete);

  using_history();
  stifle_history(MAX_HISTORY_ENTRIES);

  return read_history(getHistoryPath().c_str()) == 0;
}


////////////////////////////////////////////////////////////////////////////////
/// @brief line editor shutdown
////////////////////////////////////////////////////////////////////////////////

bool V8LineEditor::close () {
  return write_history(getHistoryPath().c_str());
}


////////////////////////////////////////////////////////////////////////////////
/// @brief get the history file path
////////////////////////////////////////////////////////////////////////////////

string V8LineEditor::getHistoryPath () {
  string path;
  
  if (getenv("HOME")) {
    path.append(getenv("HOME"));
    path += '/';
  }
  path.append(HISTORY_FILENAME); 

  return path;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief line editor prompt
////////////////////////////////////////////////////////////////////////////////

char* V8LineEditor::prompt (char const* prompt) {
  string dotdot;
  char const* p = prompt;
  size_t len1 = strlen(prompt);
  size_t len2 = len1;

  if (len1 < 3) {
    dotdot = "> ";
    len2 = 2;
  }
  else {
    dotdot = string(len1 - 2, '.') + "> ";
  }

  char const* sep = "";

  while (true) {
    char* result = readline(p);
    p = dotdot.c_str();

    if (result == 0) {

      // give up, if the user pressed control-D on the top-most level
      if (current.empty()) {
        return 0;
      }

      // otherwise clear current content
      current.clear();
      break;
    }

    current += sep;
    sep = "\n";

    bool c1 = strncmp(result, prompt, len1) == 0;
    bool c2 = strncmp(result, dotdot.c_str(), len2) == 0;

    while (c1 || c2) {
      if (c1) {
        result += len1;
      }
      else if (c2) {
        result += len2;
      }

      c1 = strncmp(result, prompt, len1) == 0;
      c2 = strncmp(result, dotdot.c_str(), len2) == 0;
    }

    current += result;
    bool ok = CheckJavaScript(current);

    if (ok) {
      break;
    }
  }

  char* line = TRI_DuplicateString(current.c_str());
  current.clear();

  return line;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add to history
////////////////////////////////////////////////////////////////////////////////

void V8LineEditor::addHistory (const char* str) {
  if (*str == '\0') {
    return;
  }

  history_set_pos(history_length-1);

  if (current_history()) {
    do {
      if (strcmp(current_history()->line, str) == 0) {
        remove_history(where_history());
        break;
      }
    }
    while (previous_history());
  }

  add_history(str);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                              CONVERSION FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Utils
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a null into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueNull (TRI_shaper_t* shaper, TRI_shape_value_t* dst) {
  dst->_type = TRI_SHAPE_NULL;
  dst->_sid = shaper->_sidNull;
  dst->_fixedSized = true;
  dst->_size = 0;
  dst->_value = 0;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a boolean into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueBoolean (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::Boolean> json) {
  TRI_shape_boolean_t* ptr;

  dst->_type = TRI_SHAPE_BOOLEAN;
  dst->_sid = shaper->_sidBoolean;
  dst->_fixedSized = true;
  dst->_size = sizeof(TRI_shape_boolean_t);
  dst->_value = (char*)(ptr = (TRI_shape_boolean_t*) TRI_Allocate(dst->_size));

  *ptr = json->Value() ? 1 : 0;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a boolean into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueBoolean (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::BooleanObject> json) {
  TRI_shape_boolean_t* ptr;

  dst->_type = TRI_SHAPE_BOOLEAN;
  dst->_sid = shaper->_sidBoolean;
  dst->_fixedSized = true;
  dst->_size = sizeof(TRI_shape_boolean_t);
  dst->_value = (char*)(ptr = (TRI_shape_boolean_t*) TRI_Allocate(dst->_size));

  *ptr = json->BooleanValue() ? 1 : 0;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a number into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueNumber (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::Number> json) {
  TRI_shape_number_t* ptr;

  dst->_type = TRI_SHAPE_NUMBER;
  dst->_sid = shaper->_sidNumber;
  dst->_fixedSized = true;
  dst->_size = sizeof(TRI_shape_number_t);
  dst->_value = (char*)(ptr = (TRI_shape_number_t*) TRI_Allocate(dst->_size));

  *ptr = json->Value();

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a number into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueNumber (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::NumberObject> json) {
  TRI_shape_number_t* ptr;

  dst->_type = TRI_SHAPE_NUMBER;
  dst->_sid = shaper->_sidNumber;
  dst->_fixedSized = true;
  dst->_size = sizeof(TRI_shape_number_t);
  dst->_value = (char*)(ptr = (TRI_shape_number_t*) TRI_Allocate(dst->_size));

  *ptr = json->NumberValue();

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a string into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueString (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::String> json) {
  char* ptr;

  v8::String::Utf8Value str(json);

  if (*str == 0) {
    dst->_type = TRI_SHAPE_SHORT_STRING;
    dst->_sid = shaper->_sidShortString;
    dst->_fixedSized = true;
    dst->_size = sizeof(TRI_shape_length_short_string_t) + TRI_SHAPE_SHORT_STRING_CUT;
    dst->_value = (ptr = (char*) TRI_Allocate(dst->_size));

    * ((TRI_shape_length_short_string_t*) ptr) = 1;
    * (ptr + sizeof(TRI_shape_length_short_string_t)) = '\0';
  }
  else if (str.length() < TRI_SHAPE_SHORT_STRING_CUT) { // includes '\0'
    size_t size = str.length() + 1;

    dst->_type = TRI_SHAPE_SHORT_STRING;
    dst->_sid = shaper->_sidShortString;
    dst->_fixedSized = true;
    dst->_size = sizeof(TRI_shape_length_short_string_t) + TRI_SHAPE_SHORT_STRING_CUT;
    dst->_value = (ptr = (char*) TRI_Allocate(dst->_size));

    * ((TRI_shape_length_short_string_t*) ptr) = size;

    memcpy(ptr + sizeof(TRI_shape_length_short_string_t), *str, size);
  }
  else {
    size_t size = str.length() + 1;

    dst->_type = TRI_SHAPE_LONG_STRING;
    dst->_sid = shaper->_sidLongString;
    dst->_fixedSized = false;
    dst->_size = sizeof(TRI_shape_length_long_string_t) + size;
    dst->_value = (ptr = (char*) TRI_Allocate(dst->_size));

    * ((TRI_shape_length_long_string_t*) ptr) = size;

    memcpy(ptr + sizeof(TRI_shape_length_long_string_t), *str, size);
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a json list into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueList (TRI_shaper_t* shaper,
                                TRI_shape_value_t* dst,
                                v8::Handle<v8::Array> json,
                                set<int>& seenHashes,
                                vector< v8::Handle<v8::Object> >& seenObjects) {
  size_t i;
  size_t n;
  size_t total;

  TRI_shape_value_t* values;
  TRI_shape_value_t* p;
  TRI_shape_value_t* e;

  bool hs;
  bool hl;

  TRI_shape_sid_t s;
  TRI_shape_sid_t l;

  TRI_shape_sid_t* sids;
  TRI_shape_size_t* offsets;
  TRI_shape_size_t offset;

  TRI_shape_t const* found;

  char* ptr;

  // check for special case "empty list"
  n = json->Length();

  if (n == 0) {
    dst->_type = TRI_SHAPE_LIST;
    dst->_sid = shaper->_sidList;

    dst->_fixedSized = false;
    dst->_size = sizeof(TRI_shape_length_list_t);
    dst->_value = (ptr = (char*) TRI_Allocate(dst->_size));

    * (TRI_shape_length_list_t*) ptr = 0;

    return true;
  }

  // convert into TRI_shape_value_t array
  p = (values = (TRI_shape_value_t*) TRI_Allocate(sizeof(TRI_shape_value_t) * n));
  memset(values, 0, sizeof(TRI_shape_value_t) * n);

  total = 0;
  e = values + n;

  for (i = 0;  i < n;  ++i, ++p) {
    v8::Local<v8::Value> el = json->Get(i);
    bool ok = FillShapeValueJson(shaper, p, el, seenHashes, seenObjects);

    if (! ok) {
      for (e = p, p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(p->_value);
        }
      }

      TRI_Free(values);

      return false;
    }

    total += p->_size;
  }

  // check if this list is homoegenous
  hs = true;
  hl = true;

  s = values[0]._sid;
  l = values[0]._size;
  p = values;

  for (;  p < e;  ++p) {
    if (p->_sid != s) {
      hs = false;
      break;
    }

    if (p->_size != l) {
      hl = false;
    }
  }

  // homogeneous sized
  if (hs && hl) {
    TRI_homogeneous_sized_list_shape_t* shape;

    shape = (TRI_homogeneous_sized_list_shape_t*) TRI_Allocate(sizeof(TRI_homogeneous_sized_list_shape_t));

    shape->base._size = sizeof(TRI_homogeneous_sized_list_shape_t);
    shape->base._type = TRI_SHAPE_HOMOGENEOUS_SIZED_LIST;
    shape->base._dataSize = TRI_SHAPE_SIZE_VARIABLE;
    shape->_sidEntry = s;
    shape->_sizeEntry = l;

    found = shaper->findShape(shaper, &shape->base);

    if (found == 0) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(p->_value);
        }
      }

      TRI_Free(values);
      TRI_Free(shape);

      return false;
    }

    dst->_type = found->_type;
    dst->_sid = found->_sid;

    dst->_fixedSized = false;
    dst->_size = sizeof(TRI_shape_length_list_t) + total;
    dst->_value = (ptr = (char*) TRI_Allocate(dst->_size));

    // copy sub-objects into data space
    * (TRI_shape_length_list_t*) ptr = n;
    ptr += sizeof(TRI_shape_length_list_t);

    for (p = values;  p < e;  ++p) {
      memcpy(ptr, p->_value, p->_size);
      ptr += p->_size;
    }
  }

  // homogeneous
  else if (hs) {
    TRI_homogeneous_list_shape_t* shape;

    shape = (TRI_homogeneous_list_shape_t*) TRI_Allocate(sizeof(TRI_homogeneous_list_shape_t));

    shape->base._size = sizeof(TRI_homogeneous_list_shape_t);
    shape->base._type = TRI_SHAPE_HOMOGENEOUS_LIST;
    shape->base._dataSize = TRI_SHAPE_SIZE_VARIABLE;
    shape->_sidEntry = s;

    found = shaper->findShape(shaper, &shape->base);

    if (found == 0) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(p->_value);
        }
      }

      TRI_Free(values);
      TRI_Free(shape);

      return false;
    }

    dst->_type = found->_type;
    dst->_sid = found->_sid;

    offset = sizeof(TRI_shape_length_list_t) + (n + 1) * sizeof(TRI_shape_size_t);

    dst->_fixedSized = false;
    dst->_size = offset + total;
    dst->_value = (ptr = (char*) TRI_Allocate(dst->_size));

    // copy sub-objects into data space
    * (TRI_shape_length_list_t*) ptr = n;
    ptr += sizeof(TRI_shape_length_list_t);

    offsets = (TRI_shape_size_t*) ptr;
    ptr += (n + 1) * sizeof(TRI_shape_size_t);

    for (p = values;  p < e;  ++p) {
      *offsets++ = offset;
      offset += p->_size;

      memcpy(ptr, p->_value, p->_size);
      ptr += p->_size;
    }

    *offsets = offset;
  }

  // in-homogeneous
  else {
    dst->_type = TRI_SHAPE_LIST;
    dst->_sid = shaper->_sidList;

    offset =
      sizeof(TRI_shape_length_list_t)
      + n * sizeof(TRI_shape_sid_t)
      + (n + 1) * sizeof(TRI_shape_size_t);

    dst->_fixedSized = false;
    dst->_size = offset + total;
    dst->_value = (ptr = (char*) TRI_Allocate(dst->_size));

    // copy sub-objects into data space
    * (TRI_shape_length_list_t*) ptr = n;
    ptr += sizeof(TRI_shape_length_list_t);

    sids = (TRI_shape_sid_t*) ptr;
    ptr += n * sizeof(TRI_shape_sid_t);

    offsets = (TRI_shape_size_t*) ptr;
    ptr += (n + 1) * sizeof(TRI_shape_size_t);

    for (p = values;  p < e;  ++p) {
      *sids++ = p->_sid;

      *offsets++ = offset;
      offset += p->_size;

      memcpy(ptr, p->_value, p->_size);
      ptr += p->_size;
    }

    *offsets = offset;
  }

  // free TRI_shape_value_t array
  for (p = values;  p < e;  ++p) {
    if (p->_value != 0) {
      TRI_Free(p->_value);
    }
  }

  TRI_Free(values);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a json array into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueArray (TRI_shaper_t* shaper,
                                 TRI_shape_value_t* dst,
                                 v8::Handle<v8::Object> json,
                                 set<int>& seenHashes,
                                 vector< v8::Handle<v8::Object> >& seenObjects) {
  size_t n;
  size_t i;
  size_t total;

  size_t f;
  size_t v;

  TRI_shape_value_t* values;
  TRI_shape_value_t* p;
  TRI_shape_value_t* e;

  TRI_array_shape_t* a;

  TRI_shape_sid_t* sids;
  TRI_shape_aid_t* aids;
  TRI_shape_size_t* offsetsF;
  TRI_shape_size_t* offsetsV;
  TRI_shape_size_t offset;

  TRI_shape_t const* found;

  char* ptr;

  // number of attributes
  v8::Handle<v8::Array> names = json->GetOwnPropertyNames();
  n = names->Length();

  // convert into TRI_shape_value_t array
  p = (values = (TRI_shape_value_t*) TRI_Allocate(n * sizeof(TRI_shape_value_t)));
  memset(values, 0, n * sizeof(TRI_shape_value_t));

  total = 0;
  f = 0;
  v = 0;

  for (i = 0;  i < n;  ++i, ++p) {
    v8::Handle<v8::Value> key = names->Get(i);
    v8::Handle<v8::Value> val = json->Get(key);

    // first find an identifier for the name
    v8::String::Utf8Value keyStr(key);

    if (*keyStr == 0) {
      --p;
      continue;
    }

    if (TRI_EqualString(*keyStr, "_id")) {
      --p;
      continue;
    }

    p->_aid = shaper->findAttributeName(shaper, *keyStr);

    // convert value
    bool ok;

    if (p->_aid == 0) {
      ok = false;
    }
    else {
      ok = FillShapeValueJson(shaper, p, val, seenHashes, seenObjects);
    }

    if (! ok) {
      for (e = p, p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(p->_value);
        }
      }

      TRI_Free(values);
      return false;
    }

    total += p->_size;

    // count fixed and variable sized values
    if (p->_fixedSized) {
      ++f;
    }
    else {
      ++v;
    }
  }

  n = f + v;

  // add variable offset table size
  total += (v + 1) * sizeof(TRI_shape_size_t);

  // now sort the shape entries
  TRI_SortShapeValues(values, n);

#ifdef DEBUG_JSON_SHAPER
  printf("shape values\n------------\ntotal: %u, fixed: %u, variable: %u\n",
         (unsigned int) n,
         (unsigned int) f,
         (unsigned int) v);
  PrintShapeValues(values, n);
  printf("\n");
#endif

  // generate shape structure
  i =
    sizeof(TRI_array_shape_t)
    + n * sizeof(TRI_shape_sid_t)
    + n * sizeof(TRI_shape_aid_t)
    + (f + 1) * sizeof(TRI_shape_size_t);

  a = (TRI_array_shape_t*) (ptr = (char*) TRI_Allocate(i));
  memset(ptr, 0, i);

  a->base._type = TRI_SHAPE_ARRAY;
  a->base._size = i;
  a->base._dataSize = (v == 0) ? total : TRI_SHAPE_SIZE_VARIABLE;

  a->_fixedEntries = f;
  a->_variableEntries = v;

  ptr += sizeof(TRI_array_shape_t);

  // array of shape identifiers
  sids = (TRI_shape_sid_t*) ptr;
  ptr += n * sizeof(TRI_shape_sid_t);

  // array of attribute identifiers
  aids = (TRI_shape_aid_t*) ptr;
  ptr += n * sizeof(TRI_shape_aid_t);

  // array of offsets for fixed part (within the shape)
  offset = (v + 1) * sizeof(TRI_shape_size_t);
  offsetsF = (TRI_shape_size_t*) ptr;

  // fill destination (except sid)
  dst->_type = TRI_SHAPE_ARRAY;

  dst->_fixedSized = true;
  dst->_size = total;
  dst->_value = (ptr = (char*) TRI_Allocate(dst->_size));

  // array of offsets for variable part (within the value)
  offsetsV = (TRI_shape_size_t*) ptr;
  ptr += (v + 1) * sizeof(TRI_shape_size_t);

  // and fill in attributes
  e = values + n;

  for (p = values;  p < e;  ++p) {
    *aids++ = p->_aid;
    *sids++ = p->_sid;

    memcpy(ptr, p->_value, p->_size);
    ptr += p->_size;

    dst->_fixedSized &= p->_fixedSized;

    if (p->_fixedSized) {
      *offsetsF++ = offset;
      offset += p->_size;
      *offsetsF = offset;
    }
    else {
      *offsetsV++ = offset;
      offset += p->_size;
      *offsetsV = offset;
    }
  }

  // free TRI_shape_value_t array
  for (p = values;  p < e;  ++p) {
    if (p->_value != 0) {
      TRI_Free(p->_value);
    }
  }

  TRI_Free(values);

  // lookup this shape
  found = shaper->findShape(shaper, &a->base);

  if (found == 0) {
    TRI_Free(a);
    return false;
  }

  // and finally add the sid
  dst->_sid = found->_sid;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a json object into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueJson (TRI_shaper_t* shaper,
                                TRI_shape_value_t* dst,
                                v8::Handle<v8::Value> json,
                                set<int>& seenHashes,
                                vector< v8::Handle<v8::Object> >& seenObjects) {
  if (json->IsObject()) {
    v8::Handle<v8::Object> o = json->ToObject();
    int hash = o->GetIdentityHash();

    if (seenHashes.find(hash) != seenHashes.end()) {
      LOG_TRACE("found hash %d", hash);

      for (vector< v8::Handle<v8::Object> >::iterator i = seenObjects.begin();  i != seenObjects.end();  ++i) {
        if (json->StrictEquals(*i)) {
          LOG_TRACE("found duplicate for hash %d", hash);
          return FillShapeValueNull(shaper, dst);
        }
      }

      seenObjects.push_back(o);
    }
    else {
      seenHashes.insert(hash);
      seenObjects.push_back(o);
    }
  }

  if (json->IsNull()) {
    return FillShapeValueNull(shaper, dst);
  }

  if (json->IsBoolean()) {
    return FillShapeValueBoolean(shaper, dst, json->ToBoolean());
  }

  if (json->IsBooleanObject()) {
    v8::Handle<v8::BooleanObject> bo = v8::Handle<v8::BooleanObject>::Cast(json);
    return FillShapeValueBoolean(shaper, dst, bo);
  }

  if (json->IsNumber()) {
    return FillShapeValueNumber(shaper, dst, json->ToNumber());
  }

  if (json->IsNumberObject()) {
    v8::Handle<v8::NumberObject> no = v8::Handle<v8::NumberObject>::Cast(json);
    return FillShapeValueNumber(shaper, dst, no);
  }

  if (json->IsString()) {
    return FillShapeValueString(shaper, dst, json->ToString());
  }

  if (json->IsStringObject()) {
    v8::Handle<v8::StringObject> so = v8::Handle<v8::StringObject>::Cast(json);
    return FillShapeValueString(shaper, dst, so->StringValue());
  }

  if (json->IsArray()) {
    v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(json);
    return FillShapeValueList(shaper, dst, array, seenHashes, seenObjects);
  }

  if (json->IsObject()) {
    return FillShapeValueArray(shaper, dst, json->ToObject(), seenHashes, seenObjects);
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data null blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataNull (TRI_shaper_t* shaper,
                                                TRI_shape_t const* shape,
                                                char const* data,
                                                size_t size) {
  return v8::Null();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data boolean blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataBoolean (TRI_shaper_t* shaper,
                                                   TRI_shape_t const* shape,
                                                   char const* data,
                                                   size_t size) {
  bool v;

  v = (* (TRI_shape_boolean_t const*) data) != 0;

  return v ? v8::True() : v8::False();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data number blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataNumber (TRI_shaper_t* shaper,
                                                  TRI_shape_t const* shape,
                                                  char const* data,
                                                  size_t size) {
  TRI_shape_number_t v;

  v = * (TRI_shape_number_t const*) data;

  return v8::Number::New(v);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data short string blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataShortString (TRI_shaper_t* shaper,
                                                       TRI_shape_t const* shape,
                                                       char const* data,
                                                       size_t size) {
  TRI_shape_length_short_string_t l;

  l = * (TRI_shape_length_short_string_t const*) data;
  data += sizeof(TRI_shape_length_short_string_t);

  return v8::String::New(data, l - 1);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data long string blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataLongString (TRI_shaper_t* shaper,
                                                      TRI_shape_t const* shape,
                                                      char const* data,
                                                      size_t size) {
  TRI_shape_length_long_string_t l;

  l = * (TRI_shape_length_long_string_t const*) data;
  data += sizeof(TRI_shape_length_long_string_t);

  return v8::String::New(data, l - 1);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data array blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataArray (TRI_shaper_t* shaper,
                                                 TRI_shape_t const* shape,
                                                 char const* data,
                                                 size_t size) {
  TRI_array_shape_t const* s;
  TRI_shape_aid_t const* aids;
  TRI_shape_sid_t const* sids;
  TRI_shape_size_t const* offsetsF;
  TRI_shape_size_t const* offsetsV;
  TRI_shape_size_t f;
  TRI_shape_size_t i;
  TRI_shape_size_t n;
  TRI_shape_size_t v;
  char const* qtr;

  v8::Handle<v8::Object> array;

  s = (TRI_array_shape_t const*) shape;
  f = s->_fixedEntries;
  v = s->_variableEntries;
  n = f + v;

  qtr = (char const*) shape;
  array = v8::Object::New();

  qtr += sizeof(TRI_array_shape_t);

  sids = (TRI_shape_sid_t const*) qtr;
  qtr += n * sizeof(TRI_shape_sid_t);

  aids = (TRI_shape_aid_t const*) qtr;
  qtr += n * sizeof(TRI_shape_aid_t);

  offsetsF = (TRI_shape_size_t const*) qtr;

  for (i = 0;  i < f;  ++i, ++sids, ++aids, ++offsetsF) {
    TRI_shape_sid_t sid = *sids;
    TRI_shape_aid_t aid = *aids;
    TRI_shape_size_t offset;
    TRI_shape_t const* subshape;
    char const* name;
    v8::Handle<v8::Value> element;

    offset = *offsetsF;
    subshape = shaper->lookupShapeId(shaper, sid);
    name = shaper->lookupAttributeId(shaper, aid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    if (name == 0) {
      LOG_WARNING("cannot find attribute #%u", (unsigned int) aid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, offsetsF[1] - offset);

    array->Set(v8::String::New(name), element);
  }

  offsetsV = (TRI_shape_size_t const*) data;

  for (i = 0;  i < v;  ++i, ++sids, ++aids, ++offsetsV) {
    TRI_shape_sid_t sid = *sids;
    TRI_shape_aid_t aid = *aids;
    TRI_shape_size_t offset;
    TRI_shape_t const* subshape;
    char const* name;
    v8::Handle<v8::Value> element;

    offset = *offsetsV;
    subshape = shaper->lookupShapeId(shaper, sid);
    name = shaper->lookupAttributeId(shaper, aid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    if (name == 0) {
      LOG_WARNING("cannot find attribute #%u", (unsigned int) aid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, offsetsV[1] - offset);

    array->Set(v8::String::New(name), element);
  }

  return array;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data list blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataList (TRI_shaper_t* shaper,
                                                TRI_shape_t const* shape,
                                                char const* data,
                                                size_t size) {
  TRI_shape_length_list_t i;
  TRI_shape_length_list_t l;
  TRI_shape_sid_t const* sids;
  TRI_shape_size_t const* offsets;
  char const* ptr;
  v8::Handle<v8::Array> list;

  list = v8::Array::New();

  ptr = data;
  l = * (TRI_shape_length_list_t const*) ptr;

  ptr += sizeof(TRI_shape_length_list_t);
  sids = (TRI_shape_sid_t const*) ptr;

  ptr += l * sizeof(TRI_shape_sid_t);
  offsets = (TRI_shape_size_t const*) ptr;

  for (i = 0;  i < l;  ++i, ++sids, ++offsets) {
    TRI_shape_sid_t sid = *sids;
    TRI_shape_size_t offset;
    TRI_shape_t const* subshape;
    v8::Handle<v8::Value> element;

    offset = *offsets;
    subshape = shaper->lookupShapeId(shaper, sid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, offsets[1] - offset);

    list->Set(i, element);
  }

  return list;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data homogeneous list blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataHomogeneousList (TRI_shaper_t* shaper,
                                                           TRI_shape_t const* shape,
                                                           char const* data,
                                                           size_t size) {
  TRI_homogeneous_list_shape_t const* s;
  TRI_shape_length_list_t i;
  TRI_shape_length_list_t l;
  TRI_shape_sid_t sid;
  TRI_shape_size_t const* offsets;
  char const* ptr;
  v8::Handle<v8::Array> list;

  list = v8::Array::New();

  s = (TRI_homogeneous_list_shape_t const*) shape;
  sid = s->_sidEntry;

  ptr = data;
  l = * (TRI_shape_length_list_t const*) ptr;

  ptr += sizeof(TRI_shape_length_list_t);
  offsets = (TRI_shape_size_t const*) ptr;

  for (i = 0;  i < l;  ++i, ++offsets) {
    TRI_shape_size_t offset;
    TRI_shape_t const* subshape;
    v8::Handle<v8::Value> element;

    offset = *offsets;
    subshape = shaper->lookupShapeId(shaper, sid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, offsets[1] - offset);

    list->Set(i, element);
  }

  return list;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data homogeneous sized list blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataHomogeneousSizedList (TRI_shaper_t* shaper,
                                                                TRI_shape_t const* shape,
                                                                char const* data,
                                                                size_t size) {
  TRI_homogeneous_sized_list_shape_t const* s;
  TRI_shape_length_list_t i;
  TRI_shape_length_list_t l;
  TRI_shape_sid_t sid;
  TRI_shape_size_t length;
  TRI_shape_size_t offset;
  char const* ptr;
  v8::Handle<v8::Array> list;

  list = v8::Array::New();

  s = (TRI_homogeneous_sized_list_shape_t const*) shape;
  sid = s->_sidEntry;

  ptr = data;
  l = * (TRI_shape_length_list_t const*) ptr;

  length = s->_sizeEntry;
  offset = sizeof(TRI_shape_length_list_t);

  for (i = 0;  i < l;  ++i, offset += length) {
    TRI_shape_t const* subshape;
    v8::Handle<v8::Value> element;

    subshape = shaper->lookupShapeId(shaper, sid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, length);

    list->Set(i, element);
  }

  return list;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeData (TRI_shaper_t* shaper,
                                            TRI_shape_t const* shape,
                                            char const* data,
                                            size_t size) {
  if (shape == 0) {
    return v8::Null();
  }

  switch (shape->_type) {
    case TRI_SHAPE_NULL:
      return JsonShapeDataNull(shaper, shape, data, size);

    case TRI_SHAPE_BOOLEAN:
      return JsonShapeDataBoolean(shaper, shape, data, size);

    case TRI_SHAPE_NUMBER:
      return JsonShapeDataNumber(shaper, shape, data, size);

    case TRI_SHAPE_SHORT_STRING:
      return JsonShapeDataShortString(shaper, shape, data, size);

    case TRI_SHAPE_LONG_STRING:
      return JsonShapeDataLongString(shaper, shape, data, size);

    case TRI_SHAPE_ARRAY:
      return JsonShapeDataArray(shaper, shape, data, size);

    case TRI_SHAPE_LIST:
      return JsonShapeDataList(shaper, shape, data, size);

    case TRI_SHAPE_HOMOGENEOUS_LIST:
      return JsonShapeDataHomogeneousList(shaper, shape, data, size);

    case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST:
      return JsonShapeDataHomogeneousSizedList(shaper, shape, data, size);
  }

  return v8::Null();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts identifier into ibject reference
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ObjectReference (TRI_voc_cid_t cid, TRI_voc_did_t did) {
  v8::HandleScope scope;
  TRI_string_buffer_t buffer;

  TRI_InitStringBuffer(&buffer);
  TRI_AppendUInt64StringBuffer(&buffer, cid);
  TRI_AppendStringStringBuffer(&buffer, ":");
  TRI_AppendUInt64StringBuffer(&buffer, did);

  v8::Handle<v8::String> ref = v8::String::New(buffer._buffer);

  TRI_DestroyStringBuffer(&buffer);

  return scope.Close(ref);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Utils
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t NULL into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> ObjectJsonNull (TRI_json_t const* json) {
  return v8::Null();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t BOOLEAN into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> ObjectJsonBoolean (TRI_json_t const* json) {
  return json->_value._boolean ? v8::True() : v8::False();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t NUMBER into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> ObjectJsonNumber (TRI_json_t const* json) {
  return v8::Number::New(json->_value._number);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t NUMBER into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> ObjectJsonString (TRI_json_t const* json) {
  return v8::String::New(json->_value._string.data);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t ARRAY into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> ObjectJsonArray (TRI_json_t const* json) {
  v8::Handle<v8::Object> object = v8::Object::New();

  size_t n = json->_value._objects._length;

  for (size_t i = 0;  i < n;  i += 2) {
    TRI_json_t* key = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i);

    if (key->_type != TRI_JSON_STRING) {
      continue;
    }

    TRI_json_t* j = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i + 1);
    v8::Handle<v8::Value> val = TRI_ObjectJson(j);

    object->Set(v8::String::New(key->_value._string.data), val);
  }

    return object;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t LIST into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> ObjectJsonList (TRI_json_t const* json) {
  v8::Handle<v8::Array> object = v8::Array::New();

  size_t n = json->_value._objects._length;

  for (size_t i = 0;  i < n;  ++i) {
    TRI_json_t* j = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i);
    v8::Handle<v8::Value> val = TRI_ObjectJson(j);

    object->Set(i, val);
  }

    return object;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> TRI_ObjectJson (TRI_json_t const* json) {
  v8::HandleScope scope;

  switch (json->_type) {
    case TRI_JSON_UNUSED:
      return scope.Close(v8::Undefined());

    case TRI_JSON_NULL:
      return scope.Close(ObjectJsonNull(json));

    case TRI_JSON_BOOLEAN:
      return scope.Close(ObjectJsonBoolean(json));

    case TRI_JSON_NUMBER:
      return scope.Close(ObjectJsonNumber(json));

    case TRI_JSON_STRING:
      return scope.Close(ObjectJsonString(json));

    case TRI_JSON_ARRAY:
      return scope.Close(ObjectJsonArray(json));

    case TRI_JSON_LIST:
      return scope.Close(ObjectJsonList(json));
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_rs_entry_t into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> TRI_ObjectRSEntry (TRI_doc_collection_t* collection,
                                         TRI_shaper_t* shaper,
                                         TRI_rs_entry_t const* entry) {
  TRI_shape_t const* shape;
  TRI_v8_global_t* v8g;

  v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  shape = shaper->lookupShapeId(shaper, entry->_document._sid);

  if (shape == 0) {
    LOG_WARNING("cannot find shape #%u", (unsigned int) entry->_document._sid);
    return v8::Null();
  }

  v8::Handle<v8::Value> result = JsonShapeData(shaper,
                                               shape,
                                               entry->_document._data.data,
                                               entry->_document._data.length);

  if (result->IsObject()) {
    result->ToObject()->Set(v8g->DidKey, ObjectReference(collection->base._cid, entry->_did));

    if (entry->_type == TRI_DOC_MARKER_EDGE) {
      result->ToObject()->Set(v8g->FromKey, ObjectReference(entry->_fromCid, entry->_fromDid));
      result->ToObject()->Set(v8g->ToKey, ObjectReference(entry->_toCid, entry->_toDid));
    }
  }

  if (entry->_augmented._type != TRI_JSON_UNUSED) {
    TRI_AugmentObject(result, &entry->_augmented);
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_shaped_json_t into a V8 object
////////////////////////////////////////////////////////////////////////////////

bool TRI_ObjectDocumentPointer (TRI_doc_collection_t* collection,
                                TRI_doc_mptr_t const* document,
                                void* storage) {
  TRI_df_marker_type_t type;
  TRI_doc_edge_marker_t* marker;
  TRI_shape_t const* shape;
  TRI_shaped_json_t const* shaped;
  TRI_shaper_t* shaper;
  TRI_v8_global_t* v8g;

  v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  shaper = collection->_shaper;
  shaped = &document->_document;
  shape = shaper->lookupShapeId(shaper, shaped->_sid);

  if (shape == 0) {
    LOG_WARNING("cannot find shape #%u", (unsigned int) shaped->_sid);
    return false;
  }

  v8::Handle<v8::Value> result = JsonShapeData(shaper,
                                               shape,
                                               shaped->_data.data,
                                               shaped->_data.length);

  if (result->IsObject()) {
    result->ToObject()->Set(v8g->DidKey, ObjectReference(collection->base._cid, document->_did));

    type = ((TRI_df_marker_t*) document->_data)->_type;

    if (type == TRI_DOC_MARKER_EDGE) {
      marker = (TRI_doc_edge_marker_t*) document->_data;

      result->ToObject()->Set(v8g->FromKey, ObjectReference(marker->_fromCid, marker->_fromDid));
      result->ToObject()->Set(v8g->ToKey, ObjectReference(marker->_toCid, marker->_toDid));
    }
  }

  * (v8::Handle<v8::Value>*) storage = result;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_result_set_t into a V8 array
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Array> TRI_ArrayResultSet (TRI_result_set_t* rs) {
  v8::Handle<v8::Array> array = v8::Array::New();

  TRI_doc_collection_t* collection = rs->_containerElement->_container->_collection;
  TRI_shaper_t* shaper = collection->_shaper;

  size_t pos;

  for (pos = 0;  rs->hasNext(rs);  ++pos) {
    TRI_rs_entry_t const* entry = rs->next(rs);

    array->Set(pos, TRI_ObjectRSEntry(collection, shaper, entry));
  }

  return array;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a TRI_shaped_json_t
////////////////////////////////////////////////////////////////////////////////

TRI_shaped_json_t* TRI_ShapedJsonV8Object (v8::Handle<v8::Value> object, TRI_shaper_t* shaper) {
  TRI_shape_value_t dst;
  set<int> seenHashes;
  vector< v8::Handle<v8::Object> > seenObjects;
  bool ok = FillShapeValueJson(shaper, &dst, object, seenHashes, seenObjects);

  if (! ok) {
    return 0;
  }

  TRI_shaped_json_t* shaped = (TRI_shaped_json_t*) TRI_Allocate(sizeof(TRI_shaped_json_t));

  shaped->_sid = dst._sid;
  shaped->_data.length = dst._size;
  shaped->_data.data = dst._value;

  return shaped;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a string
////////////////////////////////////////////////////////////////////////////////

string TRI_ObjectToString (v8::Handle<v8::Value> value) {
  v8::String::Utf8Value utf8Value(value);

  if (*utf8Value == 0) {
    return "";
  }
  else {
    return string(*utf8Value, utf8Value.length());
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a character
////////////////////////////////////////////////////////////////////////////////

char TRI_ObjectToCharacter (v8::Handle<v8::Value> value, bool& error) {
  error = false;

  if (! value->IsString() && ! value->IsStringObject()) {
    error = true;
    return '\0';
  }

  v8::String::Utf8Value sep(value->ToString());

  if (*sep == 0 || sep.length() != 1) {
    error = true;
    return '\0';
  }

  return (*sep)[0];
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a double
////////////////////////////////////////////////////////////////////////////////

double TRI_ObjectToDouble (v8::Handle<v8::Value> value) {
  if (value->IsNumber()) {
    return value->ToNumber()->Value();
  }

  if (value->IsNumberObject()) {
    v8::Handle<v8::NumberObject> no = v8::Handle<v8::NumberObject>::Cast(value);
    return no->NumberValue();
  }

  return 0.0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a double
////////////////////////////////////////////////////////////////////////////////

double TRI_ObjectToDouble (v8::Handle<v8::Value> value, bool& error) {
  error = false;

  if (value->IsNumber()) {
    return value->ToNumber()->Value();
  }

  if (value->IsNumberObject()) {
    v8::Handle<v8::NumberObject> no = v8::Handle<v8::NumberObject>::Cast(value);
    return no->NumberValue();
  }

  error = true;

  return 0.0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a boolean
////////////////////////////////////////////////////////////////////////////////

bool TRI_ObjectToBoolean (v8::Handle<v8::Value> value) {
  if (value->IsBoolean()) {
    return value->ToBoolean()->Value();
  }
  else if (value->IsBooleanObject()) {
    v8::Handle<v8::BooleanObject> bo = v8::Handle<v8::BooleanObject>::Cast(value);
    return bo->BooleanValue();
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 EXECUTION CONTEXT
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Utils
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief execution context
////////////////////////////////////////////////////////////////////////////////

typedef struct js_exec_context_s {
  v8::Persistent<v8::Function> _func;
  v8::Persistent<v8::Object> _arguments;
}
js_exec_context_t;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Utils
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new execution context
////////////////////////////////////////////////////////////////////////////////

TRI_js_exec_context_t TRI_CreateExecutionContext (char const* script) {
  js_exec_context_t* ctx;

  // execute script inside the context
  v8::Handle<v8::Script> compiled = v8::Script::Compile(v8::String::New(script), 
                                                        v8::String::New("--script--"));
    
  // compilation failed, print errors that happened during compilation
  if (compiled.IsEmpty()) {
    return 0;
  }
    
  // compute the function
  v8::Handle<v8::Value> val = compiled->Run();
    
  if (val.IsEmpty()) {
    return 0;
  }

  ctx = new js_exec_context_t;

  ctx->_func = v8::Persistent<v8::Function>::New(v8::Handle<v8::Function>::Cast(val));
  ctx->_arguments = v8::Persistent<v8::Object>::New(v8::Object::New());

  // return the handle
  return (TRI_js_exec_context_t) ctx;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Utils
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief defines a document
////////////////////////////////////////////////////////////////////////////////

bool TRI_DefineDocumentExecutionContext (TRI_js_exec_context_t context,
                                         char const* name,
                                         TRI_doc_collection_t* collection,
                                         TRI_doc_mptr_t const* document) {
  js_exec_context_t* ctx;
  v8::Handle<v8::Value> result;

  ctx = (js_exec_context_t*) context;

  bool ok = TRI_ObjectDocumentPointer(collection, document, &result);

  if (! ok) {
    return false;
  }

  ctx->_arguments->Set(v8::String::New(name), result);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes and destroys an execution context
////////////////////////////////////////////////////////////////////////////////

bool TRI_ExecuteExecutionContext (TRI_js_exec_context_t context, void* storage) {
  js_exec_context_t* ctx;

  ctx = (js_exec_context_t*) context;

  // convert back into a handle
  v8::Persistent<v8::Function> func = ctx->_func;

  // and execute the function
  v8::Handle<v8::Value> args[] = { ctx->_arguments };
  v8::Handle<v8::Value> result = func->Call(func, 1, args);
  
  if (result.IsEmpty()) {
    return false;
  }

  * (v8::Handle<v8::Value>*) storage = result;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes and destroys an execution context for a condition
////////////////////////////////////////////////////////////////////////////////

bool TRI_ExecuteConditionExecutionContext (TRI_js_exec_context_t context, bool* r) {
  js_exec_context_t* ctx;

  ctx = (js_exec_context_t*) context;

  // convert back into a handle
  v8::Persistent<v8::Function> func = ctx->_func;

  // and execute the function
  v8::Handle<v8::Value> args[] = { ctx->_arguments };
  v8::Handle<v8::Value> result = func->Call(func, 1, args);
  
  if (result.IsEmpty()) {
    return false;
  }

  *r = TRI_ObjectToBoolean(result);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                           GENERAL
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                      JS functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Utils
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a script
///
/// @FUN{internal.execute(@FA{script}, @FA{sandbox}, @FA{filename})}
///
/// Executes the @FA{script} with the @FA{sandbox} as context. Global variables
/// assigned inside the @FA{script}, will be visible in the @FA{sandbox} object
/// after execution. The @FA{filename} is used for displaying error
/// messages.
///
/// If @FA{sandbox} is undefined, then @FN{execute} uses the current context.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Execute (v8::Arguments const& argv) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;
  size_t i;

  // extract arguments
  if (argv.Length() != 3) {
    return scope.Close(v8::ThrowException(v8::String::New("usage: execute(<script>, <sandbox>, <filename>)")));
  }

  v8::Handle<v8::Value> source = argv[0];
  v8::Handle<v8::Value> sandboxValue = argv[1];
  v8::Handle<v8::Value> filename = argv[2];

  if (! source->IsString()) {
    return scope.Close(v8::ThrowException(v8::String::New("<script> must be a string")));
  }

  bool useSandbox = sandboxValue->IsObject();
  v8::Handle<v8::Object> sandbox;
  v8::Handle<v8::Context> context;

  if (useSandbox) {
    sandbox = sandboxValue->ToObject();

    // create new context
    context = v8::Context::New();
    context->Enter();

    // copy sandbox into context
    v8::Handle<v8::Array> keys = sandbox->GetPropertyNames();

    for (i = 0; i < keys->Length(); i++) {
      v8::Handle<v8::String> key = keys->Get(v8::Integer::New(i))->ToString();
      v8::Handle<v8::Value> value = sandbox->Get(key);

      if (TRI_IsTraceLogging(__FILE__)) {
        v8::String::Utf8Value keyName(key);

        if (*keyName != 0) {
          LOG_TRACE("copying key '%s' from sandbox to context", *keyName);
        }
      }

      if (value == sandbox) {
        value = context->Global();
      }

      context->Global()->Set(key, value);
    }
  }

  // execute script inside the context
  v8::Handle<v8::Script> script = v8::Script::Compile(source->ToString(), filename);

  // compilation failed, print errors that happened during compilation
  if (script.IsEmpty()) {
    assert(tryCatch.HasCaught());

    if (useSandbox) {
      context->DetachGlobal();
      context->Exit();
    }

    return scope.Close(tryCatch.ReThrow());
  }

  // compilation succeeded, run the script
  v8::Handle<v8::Value> result = script->Run();

  if (result.IsEmpty()) {
    assert(tryCatch.HasCaught());

    if (useSandbox) {
      context->DetachGlobal();
      context->Exit();
    }

    return scope.Close(tryCatch.ReThrow());
  }

  // copy result back into the sandbox
  if (useSandbox) {
    v8::Handle<v8::Array> keys = context->Global()->GetPropertyNames();

    for (i = 0; i < keys->Length(); i++) {
      v8::Handle<v8::String> key = keys->Get(v8::Integer::New(i))->ToString();
      v8::Handle<v8::Value> value = context->Global()->Get(key);

      if (TRI_IsTraceLogging(__FILE__)) {
        v8::String::Utf8Value keyName(key);

        if (*keyName != 0) {
          LOG_TRACE("copying key '%s' from context to sandbox", *keyName);
        }
      }

      if (value == context->Global()) {
        value = sandbox;
      }

      sandbox->Set(key, value);
    }

    context->DetachGlobal();
    context->Exit();
  }

  if (useSandbox) {
    return scope.Close(v8::True());
  }
  else {
    return scope.Close(result);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reads a file and executes it
///
/// @FUN{internal.load(@FA{filename})}
///
/// Reads in a files and executes the contents in the current context.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Load (v8::Arguments const& argv) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;

  // extract arguments
  if (argv.Length() != 1) {
    return scope.Close(v8::ThrowException(v8::String::New("usage: load(<filename>)")));
  }

  v8::String::Utf8Value name(argv[0]);

  if (*name == 0) {
    return scope.Close(v8::ThrowException(v8::String::New("<filename> must be a string")));
  }

  char* content = TRI_SlurpFile(*name);

  if (content == 0) {
    return scope.Close(v8::ThrowException(v8::String::New(TRI_last_error())));
  }

  bool ok = TRI_ExecuteStringVocBase(v8::Context::GetCurrent(), v8::String::New(content), argv[0], false, true);

  return scope.Close(ok ? v8::True() : v8::False());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief logs a message
///
/// @FUN{internal.log(@FA{level}, @FA{message})}
///
/// Logs the @FA{message} at the given log @FA{level}.
///
/// Valid log-level are:
///
/// - fatal
/// - error
/// - warning
/// - info
/// - debug
/// - trace
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Log (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 2) {
    return scope.Close(v8::ThrowException(v8::String::New("usage: log(<level>, <message>)")));
  }

  v8::String::Utf8Value level(argv[0]);

  if (*level == 0) {
    return scope.Close(v8::ThrowException(v8::String::New("<level> must be a string")));
  }

  v8::String::Utf8Value message(argv[1]);

  if (*message == 0) {
    return scope.Close(v8::ThrowException(v8::String::New("<message> must be a string")));
  }

  if (TRI_CaseEqualString(*level, "fatal")) {
    LOG_FATAL("%s", *message);
  }
  else if (TRI_CaseEqualString(*level, "error")) {
    LOG_ERROR("%s", *message);
  }
  else if (TRI_CaseEqualString(*level, "warning")) {
    LOG_WARNING("%s", *message);
  }
  else if (TRI_CaseEqualString(*level, "info")) {
    LOG_INFO("%s", *message);
  }
  else if (TRI_CaseEqualString(*level, "debug")) {
    LOG_DEBUG("%s", *message);
  }
  else if (TRI_CaseEqualString(*level, "trace")) {
    LOG_TRACE("%s", *message);
  }
  else {
    LOG_ERROR("(unknown log level '%s') %s", *level, *message);
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief gets or sets the log-level
///
/// @FUN{internal.logLevel()}
///
/// Returns the current log-level as string.
///
/// @verbinclude fluent37
///
/// @FUN{internal.logLevel(@FA{level})}
///
/// Changes the current log-level. Valid log-level are:
///
/// - fatal
/// - error
/// - warning
/// - info
/// - debug
/// - trace
///
/// @verbinclude fluent38
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_LogLevel (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (1 <= argv.Length()) {
    v8::String::Utf8Value str(argv[0]);

    TRI_SetLogLevelLogging(*str);
  }

  return scope.Close(v8::String::New(TRI_LogLevelLogging()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief outputs the arguments
///
/// @FUN{internal.output(@FA{string1}, @FA{string2}, @FA{string3}, ...)}
///
/// Outputs the arguments to standard output.
///
/// @verbinclude fluent39
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Output (v8::Arguments const& argv) {
  for (int i = 0; i < argv.Length(); i++) {
    v8::HandleScope scope;

    // extract the next argument
    v8::Handle<v8::Value> val = argv[i];

    // convert it into a string
    v8::String::Utf8Value utf8(val);

    if (*utf8 == 0) {
      continue;
    }

    // print the argument
    char const* ptr = *utf8;
    size_t len = utf8.length();

    while (0 < len) {
      ssize_t n = write(1, ptr, len);

      if (n < 0) {
        return v8::Undefined();
      }

      len -= n;
      ptr += n;
    }
  }

  return v8::Undefined();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reads in a file
///
/// @FUN{internal.read(@FA{filename})}
///
/// Reads in a files and returns the content as string.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Read (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 1) {
    return scope.Close(v8::ThrowException(v8::String::New("usage: read(<filename>)")));
  }

  v8::String::Utf8Value name(argv[0]);

  if (*name == 0) {
    return scope.Close(v8::ThrowException(v8::String::New("<filename> must be a string")));
  }

  char* content = TRI_SlurpFile(*name);

  if (content == 0) {
    return scope.Close(v8::ThrowException(v8::String::New(TRI_last_error())));
  }

  v8::Handle<v8::String> result = v8::String::New(content);

  TRI_FreeString(content);

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief formats the arguments
///
/// @FUN{internal.sprintf(@FA{format}, @FA{argument1}, ...)}
///
/// Formats the arguments according to the format string @FA{format}.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_SPrintF (v8::Arguments const& argv) {
  v8::HandleScope scope;

  size_t len = argv.Length();

  if (len == 0) {
    return scope.Close(v8::String::New(""));
  }

  v8::String::Utf8Value format(argv[0]);

  if (*format == 0) {
    return scope.Close(v8::ThrowException(v8::String::New("<format> must be a string")));
  }

  string result;

  size_t p = 1;

  for (char const* ptr = *format;  *ptr;  ++ptr) {
    if (*ptr == '%') {
      ++ptr;

      switch (*ptr) {
        case '%':
          result += '%';
          break;

        case 'd':
        case 'f':
        case 'i': {
          if (len <= p) {
            return scope.Close(v8::ThrowException(v8::String::New("not enough arguments")));
          }

          bool e;
          double f = TRI_ObjectToDouble(argv[p], e);

          if (e) {
            string msg = StringUtils::itoa(p) + ".th argument must be a number";
            return scope.Close(v8::ThrowException(v8::String::New(msg.c_str())));
          }

          char b[1024];

          if (*ptr == 'f') {
            snprintf(b, sizeof(b), "%f", f);
          }
          else {
            snprintf(b, sizeof(b), "%ld", (long) f);
          }

          ++p;

          result += b;

          break;
        }

        case 'o':
        case 's': {
          if (len <= p) {
            return scope.Close(v8::ThrowException(v8::String::New("not enough arguments")));
          }

          v8::String::Utf8Value text(argv[p]);

          if (*text == 0) {
            string msg = StringUtils::itoa(p) + ".th argument must be a string";
            return scope.Close(v8::ThrowException(v8::String::New(msg.c_str())));
          }

          ++p;

          result += *text;

          break;
        }

        default: {
          string msg = "found illegal format directive '" + string(1, *ptr) + "'";
          return scope.Close(v8::ThrowException(v8::String::New(msg.c_str())));
        }
      }
    }
    else {
      result += *ptr;
    }
  }

  for (size_t i = p;  i < len;  ++i) {
    v8::String::Utf8Value text(argv[i]);

    if (*text == 0) {
      string msg = StringUtils::itoa(i) + ".th argument must be a string";
      return scope.Close(v8::ThrowException(v8::String::New(msg.c_str())));
    }

    result += " ";
    result += *text;
  }

  return scope.Close(v8::String::New(result.c_str()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the current time
///
/// @FUN{internal.time()}
///
/// Returns the current time in seconds.
///
/// @verbinclude fluent36
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Time (v8::Arguments const& argv) {
  v8::HandleScope scope;

  return scope.Close(v8::Number::New(TRI_microtime()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if a file of any type or directory exists
///
/// @FUN{fs.exists(@FA{filename})}
///
/// Returns true if a file (of any type) or a directory exists at a given
/// path. If the file is a broken symbolic link, returns false.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Exists (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // extract arguments
  if (argv.Length() != 1) {
    return scope.Close(v8::ThrowException(v8::String::New("exists: execute(<filename>)")));
  }

  v8::Handle<v8::Value> filename = argv[0];

  if (! filename->IsString()) {
    return scope.Close(v8::ThrowException(v8::String::New("<filename> must be a string")));
  }

  v8::String::Utf8Value name(filename);

  if (*name == 0) {
    return scope.Close(v8::ThrowException(v8::String::New("<filename> must be an UTF8 string")));
  }

  return scope.Close(TRI_ExistsFile(*name) ? v8::True() : v8::False());;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Utils
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief adds attributes to array
////////////////////////////////////////////////////////////////////////////////

void TRI_AugmentObject (v8::Handle<v8::Value> value, TRI_json_t const* json) {
  v8::HandleScope scope;

  if (! value->IsObject()) {
    return;
  }

  if (json->_type != TRI_JSON_ARRAY) {
    return;
  }

  v8::Handle<v8::Object> object = value->ToObject();

  size_t n = json->_value._objects._length;

  for (size_t i = 0;  i < n;  i += 2) {
    TRI_json_t* key = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i);

    if (key->_type != TRI_JSON_STRING) {
      continue;
    }

    TRI_json_t* j = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i + 1);
    v8::Handle<v8::Value> val = TRI_ObjectJson(j);

    object->Set(v8::String::New(key->_value._string.data), val);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reports an exception
////////////////////////////////////////////////////////////////////////////////

string TRI_ReportV8Exception (v8::TryCatch* tryCatch) {
  v8::HandleScope handle_scope;

  v8::String::Utf8Value exception(tryCatch->Exception());
  const char* exceptionString = *exception;
  v8::Handle<v8::Message> message = tryCatch->Message();
  string result;

  // V8 didn't provide any extra information about this error; just print the exception.
  if (message.IsEmpty()) {
    if (exceptionString == 0) {
      LOG_ERROR("JavaScript exception");
      result = "JavaScript exception";
    }
    else {
      LOG_ERROR("JavaScript exception: %s", exceptionString);
      result = "JavaScript exception: " + string(exceptionString);
    }
  }
  else {
    v8::String::Utf8Value filename(message->GetScriptResourceName());
    const char* filenameString = *filename;
    int linenum = message->GetLineNumber();
    int start = message->GetStartColumn() + 1;

    if (filenameString == 0) {
      if (exceptionString == 0) {
        LOG_ERROR("JavaScript exception");
        result = "JavaScript exception";
      }
      else {
        LOG_ERROR("JavaScript exception: %s", exceptionString);
        result = "JavaScript exception: " + string(exceptionString);
      }
    }
    else {
      if (exceptionString == 0) {
        LOG_ERROR("JavaScript exception in file '%s' at %d,%d", filenameString, linenum, start);
        result = "JavaScript exception in file '" + string(filenameString) + "' at "
               + StringUtils::itoa(linenum) + "," + StringUtils::itoa(start);
      }
      else {
        LOG_ERROR("JavaScript exception in file '%s' at %d,%d: %s", filenameString, linenum, start, exceptionString);
        result = "JavaScript exception in file '" + string(filenameString) + "' at "
               + StringUtils::itoa(linenum) + "," + StringUtils::itoa(start)
               + ": " + exceptionString;
      }
    }

    v8::String::Utf8Value stacktrace(tryCatch->StackTrace());

    if (*stacktrace && stacktrace.length() > 0) {
      LOG_DEBUG("stacktrace: %s", *stacktrace);
    }
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief prints an exception and stacktrace
////////////////////////////////////////////////////////////////////////////////

void TRI_PrintV8Exception (v8::TryCatch* tryCatch) {
  v8::HandleScope handle_scope;

  v8::String::Utf8Value exception(tryCatch->Exception());
  const char* exceptionString = *exception;
  v8::Handle<v8::Message> message = tryCatch->Message();

  // V8 didn't provide any extra information about this error; just print the exception.
  if (message.IsEmpty()) {
    if (exceptionString == 0) {
      printf("JavaScript exception\n");
    }
    else {
      printf("JavaScript exception: %s\n", exceptionString);
    }
  }
  else {
    v8::String::Utf8Value filename(message->GetScriptResourceName());
    const char* filenameString = *filename;
    int linenum = message->GetLineNumber();
    int start = message->GetStartColumn() + 1;
    int end = message->GetEndColumn();

    if (filenameString == 0) {
      if (exceptionString == 0) {
        printf("exception\n");
      }
      else {
        printf("exception: %s\n", exceptionString);
      }
    }
    else {
      if (exceptionString == 0) {
        printf("exception in file '%s' at %d,%d\n", filenameString, linenum, start);
      }
      else {
        printf("exception in file '%s' at %d,%d: %s\n", filenameString, linenum, start, exceptionString);
      }
    }

    v8::String::Utf8Value sourceline(message->GetSourceLine());

    if (*sourceline) {
      printf("%s\n", *sourceline);

      if (1 < start) {
        printf("%*s", start - 1, "");
      }

      string e((size_t)(end - start + 1), '^');
      printf("%s\n", e.c_str());
    }

    v8::String::Utf8Value stacktrace(tryCatch->StackTrace());

    if (*stacktrace && stacktrace.length() > 0) {
      printf("stacktrace:\n%s\n", *stacktrace);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reads a file into the current context
////////////////////////////////////////////////////////////////////////////////

bool TRI_LoadJavaScriptFile (v8::Handle<v8::Context> context, char const* filename) {
  v8::HandleScope handleScope;
  v8::TryCatch tryCatch;

  char* content = TRI_SlurpFile(filename);

  if (content == 0) {
    LOG_TRACE("cannot loaded java script file '%s': %s", filename, TRI_last_error());
    return false;
  }

  v8::Handle<v8::String> name = v8::String::New(filename);
  v8::Handle<v8::String> source = v8::String::New(content);

  TRI_FreeString(content);

  v8::Handle<v8::Script> script = v8::Script::Compile(source, name);

  // compilation failed, print errors that happened during compilation
  if (script.IsEmpty()) {
    TRI_ReportV8Exception(&tryCatch);
    return false;
  }

  // execute script
  v8::Handle<v8::Value> result = script->Run();

  if (result.IsEmpty()) {
    assert(tryCatch.HasCaught());

    // print errors that happened during execution
    TRI_ReportV8Exception(&tryCatch);

    return false;
  }

  LOG_TRACE("loaded java script file: '%s'", filename);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reads all files from a directory into the current context
////////////////////////////////////////////////////////////////////////////////

bool TRI_LoadJavaScriptDirectory (v8::Handle<v8::Context> context, char const* path) {
  TRI_vector_string_t files;
  bool result;
  regex_t re;
  size_t i;

  LOG_TRACE("loading java script directory: '%s'", path);

  files = TRI_FilesDirectory(path);

  regcomp(&re, "^(.*)\\.js$", REG_ICASE | REG_EXTENDED);

  result = true;

  for (i = 0;  i < files._length;  ++i) {
    bool ok;
    char const* filename;
    char* full;

    filename = files._buffer[i];

    if (! regexec(&re, filename, 0, 0, 0) == 0) {
      continue;
    }

    full = TRI_Concatenate2File(path, filename);

    ok = TRI_LoadJavaScriptFile(context, full);

    TRI_FreeString(full);

    result = result && ok;
  }

  TRI_DestroyVectorString(&files);
  regfree(&re);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a string within a V8 context
////////////////////////////////////////////////////////////////////////////////

bool TRI_ExecuteStringVocBase (v8::Handle<v8::Context> context,
                               v8::Handle<v8::String> source,
                               v8::Handle<v8::Value> name,
                               bool printResult,
                               bool reportExceptions) {
  TRI_v8_global_t* v8g;
  v8::HandleScope handleScope;
  v8::TryCatch tryCatch;

  v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  v8::Handle<v8::Script> script = v8::Script::Compile(source, name);

  // compilation failed, print errors that happened during compilation
  if (script.IsEmpty()) {
    if (reportExceptions) {
      TRI_PrintV8Exception(&tryCatch);
    }

    return false;
  }

  // compilation succeeded, run the script
  else {
    v8::Handle<v8::Value> result = script->Run();

    if (result.IsEmpty()) {
      assert(tryCatch.HasCaught());

      // print errors that happened during execution
      if (reportExceptions) {
        TRI_PrintV8Exception(&tryCatch);
      }

      return false;
    }
    else {
      assert(! tryCatch.HasCaught());

      // if all went well and the result wasn't undefined then print the returned value
      if (printResult && ! result->IsUndefined()) {
        v8::Handle<v8::Function> print = v8::Handle<v8::Function>::Cast(context->Global()->Get(v8g->PrintFuncName));

        v8::Handle<v8::Value> args[] = { result };
        print->Call(print, 1, args);
      }

      return true;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stores the V8 utils functions inside the global variable
////////////////////////////////////////////////////////////////////////////////

void TRI_InitV8Utils (v8::Handle<v8::Context> context, string const& path) {
  v8::HandleScope scope;

  // check the isolate
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) isolate->GetData();

  if (v8g == 0) {
    v8g = new TRI_v8_global_t;
    isolate->SetData(v8g);
  }

  // .............................................................................
  // global function names
  // .............................................................................

  if (v8g->PrintFuncName.IsEmpty()) {
    v8g->PrintFuncName = v8::Persistent<v8::String>::New(v8::String::New("print"));
  }

  // .............................................................................
  // create the global functions
  // .............................................................................

  context->Global()->Set(v8::String::New("SYS_EXECUTE"),
                         v8::FunctionTemplate::New(JS_Execute)->GetFunction(),
                         v8::ReadOnly);

  context->Global()->Set(v8::String::New("SYS_LOAD"),
                         v8::FunctionTemplate::New(JS_Load)->GetFunction(),
                         v8::ReadOnly);

  context->Global()->Set(v8::String::New("SYS_LOG"),
                         v8::FunctionTemplate::New(JS_Log)->GetFunction(),
                         v8::ReadOnly);

  context->Global()->Set(v8::String::New("SYS_LOG_LEVEL"),
                         v8::FunctionTemplate::New(JS_LogLevel)->GetFunction(),
                         v8::ReadOnly);

  context->Global()->Set(v8::String::New("SYS_OUTPUT"),
                         v8::FunctionTemplate::New(JS_Output)->GetFunction(),
                         v8::ReadOnly);

  context->Global()->Set(v8::String::New("SYS_READ"),
                         v8::FunctionTemplate::New(JS_Read)->GetFunction(),
                         v8::ReadOnly);

  context->Global()->Set(v8::String::New("SYS_SPRINTF"),
                         v8::FunctionTemplate::New(JS_SPrintF)->GetFunction(),
                         v8::ReadOnly);

  context->Global()->Set(v8::String::New("SYS_TIME"),
                         v8::FunctionTemplate::New(JS_Time)->GetFunction(),
                         v8::ReadOnly);

  context->Global()->Set(v8::String::New("FS_EXISTS"),
                         v8::FunctionTemplate::New(JS_Exists)->GetFunction(),
                         v8::ReadOnly);

  // .............................................................................
  // create the global variables
  // .............................................................................

  context->Global()->Set(v8::String::New("MODULES_PATH"),
                         v8::String::New(path.c_str()));

  // .............................................................................
  // keys
  // .............................................................................

  if (v8g->DidKey.IsEmpty()) {
    v8g->DidKey = v8::Persistent<v8::String>::New(v8::String::New("_id"));
  }

  if (v8g->FromKey.IsEmpty()) {
    v8g->FromKey = v8::Persistent<v8::String>::New(v8::String::New("_from"));
  }

  if (v8g->ToKey.IsEmpty()) {
    v8g->ToKey = v8::Persistent<v8::String>::New(v8::String::New("_to"));
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
