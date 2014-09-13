////////////////////////////////////////////////////////////////////////////////
/// @brief conversion functions
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_BASICS_C_CONVERSIONS_H
#define ARANGODB_BASICS_C_CONVERSIONS_H 1

#include "Basics/Common.h"

// -----------------------------------------------------------------------------
// --SECTION--                          public functions for string to something
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a single hex to an integer
////////////////////////////////////////////////////////////////////////////////

int TRI_IntHex (char ch, int errorValue);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to boolean from string
////////////////////////////////////////////////////////////////////////////////

bool TRI_BooleanString (char const* str);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to double from string
////////////////////////////////////////////////////////////////////////////////

double TRI_DoubleString (char const* str);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to int32 from string
////////////////////////////////////////////////////////////////////////////////

int32_t TRI_Int32String (char const* str);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to int32 from string with given length
////////////////////////////////////////////////////////////////////////////////

int32_t TRI_Int32String2 (char const* str, size_t length);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to uint32 from string
////////////////////////////////////////////////////////////////////////////////

uint32_t TRI_UInt32String (char const* str);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to uint32 from string with given length
////////////////////////////////////////////////////////////////////////////////

uint32_t TRI_UInt32String2 (char const* str, size_t length);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to int64 from string
////////////////////////////////////////////////////////////////////////////////

int64_t TRI_Int64String (char const* str);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to int64 from string with given length
////////////////////////////////////////////////////////////////////////////////

int64_t TRI_Int64String2 (char const* str, size_t length);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to uint64 from string
////////////////////////////////////////////////////////////////////////////////

uint64_t TRI_UInt64String (char const* str);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to uint64 from string with given length
////////////////////////////////////////////////////////////////////////////////

uint64_t TRI_UInt64String2 (char const* str, size_t length);

// -----------------------------------------------------------------------------
// --SECTION--                             public functions for number to string
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from int8, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringInt8InPlace (int8_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from uint8, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringUInt8InPlace (uint8_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from int16, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringInt16InPlace (int16_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from uint16, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringUInt16InPlace (uint16_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from int32, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringInt32InPlace (int32_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from uint32, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringUInt32InPlace (uint32_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from int64, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringInt64InPlace (int64_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from uint64, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringUInt64InPlace (uint64_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from int8
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringInt8 (int8_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from uint16
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringUInt8 (uint8_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from int16
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringInt16 (int16_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from uint16
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringUInt16 (uint16_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from int32
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringInt32 (int32_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from uint32
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringUInt32 (uint32_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from int64
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringInt64 (int64_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from uint64
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringUInt64 (uint64_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to string from double
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringDouble (double);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to a hex string from uint32, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringUInt32HexInPlace (uint32_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to a hex string from uint64, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringUInt64HexInPlace (uint64_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to hex string from int32
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringUInt32Hex (uint32_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to hex string from int64
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringUInt64Hex (uint64_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to an octal string from uint32, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringUInt32OctalInPlace (uint32_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to an octal string from uint64, using the specified buffer.
/// A NUL-byte will be appended at the end.
/// It is the caller's responsibility to ensure the buffer is big enough to
/// contain the result string and the NUL byte.
/// The length of the string number in characters without the NUL byte is
/// returned.
////////////////////////////////////////////////////////////////////////////////

size_t TRI_StringUInt64OctalInPlace (uint64_t, char*);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to octal string from int32
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringUInt32Octal (uint32_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief convert to octal string from int64
////////////////////////////////////////////////////////////////////////////////

char* TRI_StringUInt64Octal (uint64_t);

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
