////////////////////////////////////////////////////////////////////////////////
/// @brief AQL query/cursor request handler
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
/// @author Max Neunhoeffer
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2010-2014, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "RestAqlHandler.h"

#include "Basics/ConditionLocker.h"
#include "Basics/StringUtils.h"
#include "Rest/HttpRequest.h"
#include "Rest/HttpResponse.h"

#include "HttpServer/HttpServer.h"
#include "HttpServer/HttpHandlerFactory.h"
#include "GeneralServer/GeneralServerJob.h"
#include "GeneralServer/GeneralServer.h"

#include "VocBase/server.h"
//#include "V8Server/v8-vocbaseprivate.h"

#include "Aql/ExecutionEngine.h"
#include "Aql/ExecutionBlock.h"

using namespace triagens::basics;
using namespace triagens::arango;
using namespace triagens::rest;
using namespace triagens::aql;

// -----------------------------------------------------------------------------
// --SECTION--                                                  public constants
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief name of the queue
////////////////////////////////////////////////////////////////////////////////

const std::string RestAqlHandler::QUEUE_NAME = "STANDARD";

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

RestAqlHandler::RestAqlHandler (triagens::rest::HttpRequest* request,
                                std::pair<ApplicationV8*, 
                                QueryRegistry*>* pair)
  : RestBaseHandler(request),
    _applicationV8(pair->first),
    _context(static_cast<VocbaseContext*>(request->getRequestContext())),
    _vocbase(_context->getVocbase()),
    _queryRegistry(pair->second),
    _qId(0) {

  TRI_ASSERT(_vocbase != nullptr);
  TRI_ASSERT(_queryRegistry != nullptr);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   Handler methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool RestAqlHandler::isDirect () {
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

std::string const& RestAqlHandler::queue () const {
  return QUEUE_NAME;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief POST method for /_api/aql/instanciate
/// The body is a JSON with attributes "plan" for the execution plan and
/// "options" for the options, all exactly as in AQL_EXECUTEJSON.
////////////////////////////////////////////////////////////////////////////////

void RestAqlHandler::createQueryFromJson () {
  Json queryJson(TRI_UNKNOWN_MEM_ZONE, parseJsonBody());
  if (queryJson.isEmpty()) {
    return;
  }
  
  Json plan;
  Json options;

  plan = queryJson.get("plan").copy();   // cannot throw
  if (plan.isEmpty()) {
    generateError(HttpResponse::BAD, TRI_ERROR_INTERNAL,
      "body must be an object with attribute \"plan\"");
    return;
  }
  options = queryJson.get("options").copy();
  
  std::string const part = JsonHelper::getStringValue(queryJson.json(), "part", "");

  auto query = new Query(_vocbase, plan, options.steal(), (part == "main" ? PART_MAIN : PART_DEPENDENT));
  QueryResult res = query->prepare(_queryRegistry);
  if (res.code != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::BAD, TRI_ERROR_QUERY_BAD_JSON_PLAN,
      res.details);
    delete query;
    return;
  }

  // Now the query is ready to go, store it in the registry and return:
  double ttl = 3600.0;
  bool found;
  char const* ttlstring = _request->header("ttl", found);
  if (found) {
    ttl = StringUtils::doubleDecimal(ttlstring);
  }

  // query id not set, now generate a new one
  _qId = TRI_NewTickServer();
  
  try {
    _queryRegistry->insert(_vocbase, _qId, query, ttl);
  }
  catch (...) {
    generateError(HttpResponse::BAD, TRI_ERROR_INTERNAL,
        "could not keep query in registry");
    delete query;
    return;
  }

  _response = createResponse(triagens::rest::HttpResponse::ACCEPTED);
  _response->setContentType("application/json; charset=utf-8");
  Json answerBody(Json::Array, 2);
  answerBody("queryId", Json(StringUtils::itoa(_qId)))
            ("ttl",     Json(ttl));

 //std::cout << "RESPONSE BODY IS: " << answerBody.toString() << "\n";            
  _response->body().appendText(answerBody.toString());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief POST method for /_api/aql/parse
/// The body is a Json with attributes "query" for the query string,
/// "parameters" for the query parameters and "options" for the options.
/// This does the same as AQL_PARSE with exactly these parameters and
/// does not keep the query hanging around.
////////////////////////////////////////////////////////////////////////////////

void RestAqlHandler::parseQuery () {
  Json queryJson(TRI_UNKNOWN_MEM_ZONE, parseJsonBody());
  if (queryJson.isEmpty()) {
    return;
  }
  
  std::string const queryString = JsonHelper::getStringValue(queryJson.json(), "query", "");
  if (queryString.empty()) {
    generateError(HttpResponse::BAD, TRI_ERROR_INTERNAL,
      "body must be an object with attribute \"query\"");
    return;
  }

  auto query = new Query(_vocbase, queryString.c_str(), queryString.size(),
                         nullptr, nullptr, PART_MAIN);
  QueryResult res = query->parse();
  if (res.code != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::BAD, res.code, res.details);
    delete query;
    return;
  }

  // Now prepare the answer:
  Json answerBody(Json::Array, 4);
  answerBody("parsed", Json(true));
  Json collections(Json::List, res.collectionNames.size());
  for (auto const& c : res.collectionNames) {
    collections(Json(c));
  }
  answerBody("collections", collections);
  Json bindVars(Json::List, res.bindParameters.size());
  for (auto const& p : res.bindParameters) {
    bindVars(Json(p));
  }
  answerBody("parameters", bindVars);
  answerBody("ast", Json(res.zone, res.json));
  res.json = nullptr;

  _response = createResponse(triagens::rest::HttpResponse::OK);
  _response->setContentType("application/json; charset=utf-8");
  _response->body().appendText(answerBody.toString());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief POST method for /_api/aql/explain
/// The body is a Json with attributes "query" for the query string,
/// "parameters" for the query parameters and "options" for the options.
/// This does the same as AQL_EXPLAIN with exactly these parameters and
/// does not keep the query hanging around.
////////////////////////////////////////////////////////////////////////////////

void RestAqlHandler::explainQuery () {
  Json queryJson(TRI_UNKNOWN_MEM_ZONE, parseJsonBody());
  if (queryJson.isEmpty()) {
    return;
  }
  
  std::string queryString = JsonHelper::getStringValue(queryJson.json(), "query", "");
  if (queryString.empty()) {
    generateError(HttpResponse::BAD, TRI_ERROR_INTERNAL,
      "body must be an object with attribute \"query\"");
    return;
  }
  
  Json parameters;
  parameters = queryJson.get("parameters").copy();  // cannot throw
  Json options;
  options = queryJson.get("options").copy();        // cannot throw

  auto query = new Query(_vocbase, queryString.c_str(), queryString.size(),
                         parameters.steal(), options.steal(), PART_MAIN);
  QueryResult res = query->explain();
  if (res.code != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::BAD, res.code, res.details);
    delete query;
    return;
  }

  // Now prepare the answer:
  Json answerBody(Json::Array, 1);
  if (res.json != nullptr) {
    if (query->allPlans()) {
      answerBody("plans", Json(res.zone, res.json));
    }
    else {
      answerBody("plan", Json(res.zone, res.json));
    }
  }
  res.json = nullptr;

  _response = createResponse(triagens::rest::HttpResponse::OK);
  _response->setContentType("application/json; charset=utf-8");
  _response->body().appendText(answerBody.toString());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief POST method for /_api/aql/query
/// The body is a Json with attributes "query" for the query string,
/// "parameters" for the query parameters and "options" for the options.
/// This sets up the query as as AQL_EXECUTE would, but does not use
/// the cursor API yet. Rather, the query is stored in the query registry
/// for later use by PUT requests.
////////////////////////////////////////////////////////////////////////////////

void RestAqlHandler::createQueryFromString () {
  Json queryJson(TRI_UNKNOWN_MEM_ZONE, parseJsonBody());
  if (queryJson.isEmpty()) {
    return;
  }
  
  std::string const queryString = JsonHelper::getStringValue(queryJson.json(), "query", "");
  if (queryString.empty()) {
    generateError(HttpResponse::BAD, TRI_ERROR_INTERNAL,
      "body must be an object with attribute \"query\"");
    return;
  }

  std::string const part = JsonHelper::getStringValue(queryJson.json(), "part", "");
  if (part.empty()) {
    generateError(HttpResponse::BAD, TRI_ERROR_INTERNAL,
      "body must be an object with attribute \"part\"");
    return;
  }

  Json parameters;
  parameters = queryJson.get("parameters").copy();  // cannot throw
  Json options;
  options = queryJson.get("options").copy();        // cannot throw

  auto query = new Query(_vocbase, queryString.c_str(), queryString.size(),
                         parameters.steal(), options.steal(), (part == "main" ? PART_MAIN : PART_DEPENDENT));
  QueryResult res = query->prepare(_queryRegistry);
  if (res.code != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::BAD, TRI_ERROR_QUERY_BAD_JSON_PLAN,
      res.details);
    delete query;
    return;
  }

  // Now the query is ready to go, store it in the registry and return:
  double ttl = 3600.0;
  bool found;
  char const* ttlstring = _request->header("ttl", found);
  if (found) {
    ttl = StringUtils::doubleDecimal(ttlstring);
  }

  _qId = TRI_NewTickServer();
  try {
    _queryRegistry->insert(_vocbase, _qId, query, ttl);
  }
  catch (...) {
    generateError(HttpResponse::BAD, TRI_ERROR_INTERNAL,
        "could not keep query in registry");
    delete query;
    return;
  }

  _response = createResponse(triagens::rest::HttpResponse::ACCEPTED);
  _response->setContentType("application/json; charset=utf-8");
  Json answerBody(Json::Array, 2);
  answerBody("queryId", Json(StringUtils::itoa(_qId)))
            ("ttl",     Json(ttl));
  _response->body().appendText(answerBody.toString());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief DELETE method for /_api/aql/<queryId>
/// The query specified by <queryId> is deleted.
////////////////////////////////////////////////////////////////////////////////

void RestAqlHandler::deleteQuery (std::string const& idString) {
  // the DELETE verb
  Query* query = nullptr;
  if (findQuery(idString, query)) {
    return;
  }

  TRI_ASSERT(_qId > 0);
  _queryRegistry->destroy(_vocbase, _qId);

  _response = createResponse(triagens::rest::HttpResponse::OK);
  _response->setContentType("application/json; charset=utf-8");
  Json answerBody(Json::Array, 2);
  answerBody("error", Json(false))
            ("queryId", Json(idString));
  _response->body().appendText(answerBody.toString());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief PUT method for /_api/aql/<operation>/<queryId>, this is using 
/// the part of the cursor API with side effects.
/// <operation>: can be "getSome" or "skip" or "initializeCursor" or 
/// "shutdown".
/// The body must be a Json with the following attributes:
/// For the "getSome" operation one has to give:
///   "atLeast": 
///   "atMost": both must be positive integers, the cursor returns never 
///             more than "atMost" items and tries to return at least
///             "atLeast". Note that it is possible to return fewer than
///             "atLeast", for example if there are only fewer items
///             left. However, the implementation may return fewer items
///             than "atLeast" for internal reasons, for example to avoid
///             excessive copying. The result is the JSON representation of an 
///             AqlItemBlock.
///             If "atLeast" is not given it defaults to 1, if "atMost" is not
///             given it defaults to ExecutionBlock::DefaultBatchSize.
/// For the "skipSome" operation one has to give:
///   "atLeast": 
///   "atMost": both must be positive integers, the cursor skips never 
///             more than "atMost" items and tries to skip at least
///             "atLeast". Note that it is possible to skip fewer than
///             "atLeast", for example if there are only fewer items
///             left. However, the implementation may skip fewer items
///             than "atLeast" for internal reasons, for example to avoid
///             excessive copying. The result is a JSON object with a
///             single attribute "skipped" containing the number of
///             skipped items.
///             If "atLeast" is not given it defaults to 1, if "atMost" is not
///             given it defaults to ExecutionBlock::DefaultBatchSize.
/// For the "skip" operation one should give:
///   "number": must be a positive integer, the cursor skips as many items,
///             possibly exhausting the cursor.
///             The result is a JSON with the attributes "error" (boolean),
///             "errorMessage" (if applicable) and "exhausted" (boolean)
///             to indicate whether or not the cursor is exhausted.
///             If "number" is not given it defaults to 1.
/// For the "initializeCursor" operation, one has to bind the following 
/// attributes:
///   "items": This is a serialised AqlItemBlock with usually only one row 
///            and the correct number of columns.
///   "pos":   The number of the row in "items" to take, usually 0.
/// For the "shutdown" operation no additional arguments are required and
/// an empty JSON object in the body is OK.
/// All operations allow to set the HTTP header "Shard-ID:". If this is
/// set, then the root block of the stored query must be a ScatterBlock
/// and the shard ID is given as an additional argument to the ScatterBlock's
/// special API.
////////////////////////////////////////////////////////////////////////////////

void RestAqlHandler::useQuery (std::string const& operation,
                               std::string const& idString) {
  // the PUT verb
  Query* query = nullptr;
  if (findQuery(idString, query)) {
    return;
  }

  TRI_ASSERT(_qId > 0);
  TRI_ASSERT(query->engine() != nullptr);

  Json queryJson;
  if (operation != "shutdown") {
    // /shutdown does not require a body
    queryJson = Json(TRI_UNKNOWN_MEM_ZONE, parseJsonBody());
    if (queryJson.isEmpty()) {
      _queryRegistry->close(_vocbase, _qId);
      return;
    }
  }

  try {
    handleUseQuery(operation, query, queryJson);
    try {
      _queryRegistry->close(_vocbase, _qId);
    }
    catch (...) {
      // ignore errors on unregistering
      // an error might occur if "shutdown" is called
    }
  }
  catch (triagens::arango::Exception const& ex) {
    _queryRegistry->close(_vocbase, _qId);
    
    generateError(HttpResponse::SERVER_ERROR, 
                  ex.code(),
                  ex.message());
  }
  catch (std::exception const& ex) {
    _queryRegistry->close(_vocbase, _qId);
    
    generateError(HttpResponse::SERVER_ERROR, 
                  TRI_ERROR_HTTP_SERVER_ERROR,
                  ex.what());
  }
  catch (...) {
    _queryRegistry->close(_vocbase, _qId);
      
    generateError(HttpResponse::SERVER_ERROR, 
                  TRI_ERROR_HTTP_SERVER_ERROR,
                  "an unknown exception occurred");
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief GET method for /_api/aql/<operation>/<queryId>, this is using
/// the part of the cursor API without side effects. The operation must
/// be one of "count", "remaining" and "hasMore". The result is a Json
/// with, depending on the operation, the following attributes:
///   for "count": the result has the attributes "error" (set to false)
///                and "count" set to the total number of documents.
///   for "remaining": the result has the attributes "error" (set to false)
///                and "remaining" set to the total number of documents.
///   for "hasMore": the result has the attributes "error" (set to false)
///                  and "hasMore" set to a boolean value.
/// Note that both "count" and "remaining" may return "unknown" if the
/// internal cursor API returned -1.
////////////////////////////////////////////////////////////////////////////////

void RestAqlHandler::getInfoQuery (std::string const& operation,
                                   std::string const& idString) {
  // the GET verb

  bool found;
  std::string shardId;
  char const* shardIdCharP = _request->header("shard-id", found);
  if (found && shardIdCharP != nullptr) {
    shardId = shardIdCharP;
  }

  Query* query = nullptr;
  if (findQuery(idString, query)) {
    return;
  }

  TRI_ASSERT(_qId > 0);

  Json answerBody(Json::Array, 2);

  TRI_ASSERT(query->engine() != nullptr);

  try {
    int64_t number;
    if (operation == "count") {
      number = query->engine()->count();
      if (number == -1) {
        answerBody("count", Json("unknown"));
      }
      else {
        answerBody("count", Json(static_cast<double>(number)));
      }
    }
    else if (operation == "remaining") {
      // FIXME:
      // Do the !shardId.empty() case once the ScatterBlock has remainingForShard
      number = query->engine()->remaining();
      if (number == -1) {
        answerBody("remaining", Json("unknown"));
      }
      else {
        answerBody("remaining", Json(static_cast<double>(number)));
      }
    }
    else if (operation == "hasMore") {
      bool hasMore;
      if (shardId.empty()) {
        hasMore = query->engine()->hasMore();
      }
      else {
        auto scatter = static_cast<ScatterBlock*>(query->engine()->root());
        if (scatter->getPlanNode()->getType() != ExecutionNode::SCATTER) {
          THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
        }
        hasMore = scatter->hasMoreForShard(shardId);
      }

      answerBody("hasMore", Json(hasMore));
    }
    else {
      _queryRegistry->close(_vocbase, _qId);
      generateError(HttpResponse::NOT_FOUND, TRI_ERROR_HTTP_NOT_FOUND);
      return;
    }
  }
  catch (triagens::arango::Exception const& ex) {
    _queryRegistry->close(_vocbase, _qId);
    
    generateError(HttpResponse::SERVER_ERROR, 
                  ex.code(),
                  ex.message());
  }
  catch (std::exception const& ex) {
    _queryRegistry->close(_vocbase, _qId);
    
    generateError(HttpResponse::SERVER_ERROR, 
                  TRI_ERROR_HTTP_SERVER_ERROR,
                  ex.what());
  }
  catch (...) {
    _queryRegistry->close(_vocbase, _qId);
      
    generateError(HttpResponse::SERVER_ERROR, 
                  TRI_ERROR_HTTP_SERVER_ERROR,
                  "an unknown exception occurred");
  }

  _queryRegistry->close(_vocbase, _qId);

  _response = createResponse(triagens::rest::HttpResponse::OK);
  _response->setContentType("application/json; charset=utf-8");
  answerBody("error", Json(false));
  _response->body().appendText(answerBody.toString());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the handler
////////////////////////////////////////////////////////////////////////////////

triagens::rest::HttpHandler::status_t RestAqlHandler::execute () {
//std::cout << "GOT INCOMING REQUEST: " << triagens::rest::HttpRequest::translateMethod(_request->requestType()) << ", " << triagens::arango::ServerState::instance()->getId() << ": " << _request->fullUrl() << ": " << _request->body() << "\n";

  auto context = _applicationV8->enterContext("STANDARD", _vocbase,
                                              false, false);
  
  // assert that everything is cleaned up when we enter the context
#ifdef TRI_ENABLE_MAINTAINER_MODE  
  TRI_v8_global_t* v8g = static_cast<TRI_v8_global_t*>(context->_isolate->GetData());
  TRI_ASSERT(v8g->_currentTransaction == nullptr);
  TRI_ASSERT(v8g->_resolver == nullptr);
#endif

  if (nullptr == context) {
    generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_INTERNAL,
                  "cannot enter V8 context");
  }
  else {
    std::vector<std::string> const& suffix = _request->suffix();

    // extract the sub-request type
    HttpRequest::HttpRequestType type = _request->requestType();


    // execute one of the CRUD methods
    switch (type) {
      case HttpRequest::HTTP_REQUEST_POST: {
        if (suffix.size() != 1) {
          generateError(HttpResponse::NOT_FOUND, TRI_ERROR_HTTP_NOT_FOUND);
        }
        else if (suffix[0] == "instanciate") {
          createQueryFromJson(); 
        }
        else if (suffix[0] == "parse") {
          parseQuery();
        }
        else if (suffix[0] == "explain") {
          explainQuery();
        }
        else if (suffix[0] == "query") {
          createQueryFromString();
        }
        else {
          generateError(HttpResponse::NOT_FOUND, TRI_ERROR_HTTP_NOT_FOUND);
        }
        break;
      }
      case HttpRequest::HTTP_REQUEST_DELETE: {
        if (suffix.size() != 1) {
          generateError(HttpResponse::NOT_FOUND, TRI_ERROR_HTTP_NOT_FOUND);
        }
        else {
          deleteQuery(suffix[0]);
        }
        break;
      }
      case HttpRequest::HTTP_REQUEST_PUT: {
        if (suffix.size() != 2) {
          generateError(HttpResponse::NOT_FOUND, TRI_ERROR_HTTP_NOT_FOUND);
        }
        else {
          useQuery(suffix[0], suffix[1]);
        }
        break;
      }
      case HttpRequest::HTTP_REQUEST_GET: {
        if (suffix.size() != 2) {
          generateError(HttpResponse::NOT_FOUND, TRI_ERROR_HTTP_NOT_FOUND);
        }
        else {
          getInfoQuery(suffix[0], suffix[1]);
        }
        break;
      }
      case HttpRequest::HTTP_REQUEST_HEAD:
      case HttpRequest::HTTP_REQUEST_PATCH:
      case HttpRequest::HTTP_REQUEST_OPTIONS:
      case HttpRequest::HTTP_REQUEST_ILLEGAL: {
        generateError(HttpResponse::METHOD_NOT_ALLOWED, 
                      TRI_ERROR_NOT_IMPLEMENTED,
                      "illegal method for /_api/aql");
        break;
      }
    }

  }
  
  // must have cleaned everything up
#ifdef TRI_ENABLE_MAINTAINER_MODE  
  TRI_ASSERT(v8g->_currentTransaction == nullptr);
  TRI_ASSERT(v8g->_resolver == nullptr);
#endif

  _applicationV8->exitContext(context);
//std::cout << "REQUEST HANDLING DONE: " << triagens::arango::ServerState::instance()->getId() << ": " << _request->fullUrl() << ": " << _response->responseCode() << ", CONTENT-LENGTH: " << _response->contentLength() << "\n";

  return status_t(HANDLER_DONE);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dig out the query from ID, handle errors
////////////////////////////////////////////////////////////////////////////////

bool RestAqlHandler::findQuery (std::string const& idString,
                                Query*& query) {
  _qId = StringUtils::uint64(idString);

  query = nullptr;

  try {
    query = _queryRegistry->open(_vocbase, _qId);
  }
  catch (...) {
    _qId = 0;
    generateError(HttpResponse::FORBIDDEN, TRI_ERROR_QUERY_IN_USE);
    return true;
  }

  if (query == nullptr) {
    _qId = 0;
    generateError(HttpResponse::NOT_FOUND, TRI_ERROR_QUERY_NOT_FOUND);
    return true;
  }

  TRI_ASSERT(_qId > 0);

  return false;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief handle for useQuery
////////////////////////////////////////////////////////////////////////////////

void RestAqlHandler::handleUseQuery (std::string const& operation,
                                     Query* query,
                                     Json const& queryJson) {
  bool found;
  std::string shardId;
  char const* shardIdCharP = _request->header("shard-id", found);
  if (found && shardIdCharP != nullptr) {
    shardId = shardIdCharP;
  }

  Json answerBody(Json::Array, 2);

  if (operation == "getSome") {
    auto atLeast = JsonHelper::getNumericValue<uint64_t>(queryJson.json(),
                                                         "atLeast", 1);
    auto atMost = JsonHelper::getNumericValue<uint64_t>(queryJson.json(),
                               "atMost", ExecutionBlock::DefaultBatchSize);
    std::unique_ptr<AqlItemBlock> items;
    if (shardId.empty()) {
      items.reset(query->engine()->getSome(atLeast, atMost));
    }
    else {
      auto scatter = static_cast<ScatterBlock*>(query->engine()->root());
      if (scatter->getPlanNode()->getType() != ExecutionNode::SCATTER) {
        THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
      }
      items.reset(scatter->getSomeForShard(atLeast, atMost, shardId));
    }
    if (items.get() == nullptr) {
      answerBody("exhausted", Json(true))
                ("error", Json(false));
    }
    else {
      try {
        answerBody = items->toJson(query->trx());
//std::cout << "ANSWERBODY: " << JsonHelper::toString(answerBody.json()) << "\n\n";        
      }
      catch (...) {
        generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_HTTP_SERVER_ERROR,
                      "cannot transform AqlItemBlock to Json");
        return;
      }
    }
  }
  else if (operation == "skipSome") {
    auto atLeast = JsonHelper::getNumericValue<uint64_t>(queryJson.json(),
                                                         "atLeast", 1);
    auto atMost = JsonHelper::getNumericValue<uint64_t>(queryJson.json(),
                               "atMost", ExecutionBlock::DefaultBatchSize);
    size_t skipped;
    try {
      if (shardId.empty()) {
        skipped = query->engine()->skipSome(atLeast, atMost);
      }
      else {
        auto scatter = static_cast<ScatterBlock*>(query->engine()->root());
        if (scatter->getPlanNode()->getType() != ExecutionNode::SCATTER) {
          THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
        }
        skipped = scatter->skipSomeForShard(atLeast, atMost, shardId);
      }
    }
    catch (...) {
      generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_HTTP_SERVER_ERROR,
                    "skipSome lead to an exception");
      return;
    }
    answerBody("skipped", Json(static_cast<double>(skipped)))
              ("error", Json(false));
  }
  else if (operation == "skip") {
    auto number = JsonHelper::getNumericValue<uint64_t>(queryJson.json(),
                                                        "number", 1);
    try {
      bool exhausted;
      if (shardId.empty()) {
        exhausted = query->engine()->skip(number);
      }
      else {
        auto scatter = static_cast<ScatterBlock*>(query->engine()->root());
        if (scatter->getPlanNode()->getType() != ExecutionNode::SCATTER) {
          THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
        }
        exhausted = scatter->skipForShard(number, shardId);
      }

      answerBody("exhausted", Json(exhausted))
                ("error", Json(false));
    }
    catch (...) {
      generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_HTTP_SERVER_ERROR,
                    "skip lead to an exception");
      return;
    }
  }
  else if (operation == "initializeCursor") {
    auto pos = JsonHelper::getNumericValue<size_t>(queryJson.json(),
                                                   "pos", 0);
    std::unique_ptr<AqlItemBlock> items;
    int res;
    try {
      if (JsonHelper::getBooleanValue(queryJson.json(), "exhausted", true)) {
        res = query->engine()->initializeCursor(nullptr, 0);
      }
      else {
        items.reset(new AqlItemBlock(queryJson.get("items")));
        res = query->engine()->initializeCursor(items.get(), pos);
      }
    }
    catch (...) {
      generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_HTTP_SERVER_ERROR,
                    "initializeCursor lead to an exception");
      return;
    }
    answerBody("error", Json(res == TRI_ERROR_NO_ERROR))
              ("code", Json(static_cast<double>(res)));
  }
  else if (operation == "shutdown") {
    int res = TRI_ERROR_INTERNAL;
    try {
      res = query->engine()->shutdown();
      _queryRegistry->destroy(_vocbase, _qId);
    }
    catch (...) {
      generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_HTTP_SERVER_ERROR,
                    "shutdown lead to an exception");
      return;
    }
    answerBody("error", res == TRI_ERROR_NO_ERROR ? Json(false) : Json(true))
              ("code", Json(static_cast<double>(res)));
  }
  else {
    generateError(HttpResponse::NOT_FOUND, TRI_ERROR_HTTP_NOT_FOUND);
    return;
  }

  _response = createResponse(triagens::rest::HttpResponse::OK);
  _response->setContentType("application/json; charset=utf-8");
  _response->body().appendText(answerBody.toString());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extract the JSON from the request
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* RestAqlHandler::parseJsonBody () {
  char* errmsg = nullptr;
  TRI_json_t* json = _request->toJson(&errmsg);

  if (json == nullptr) {
    if (errmsg == nullptr) {
      generateError(HttpResponse::BAD,
                    TRI_ERROR_HTTP_CORRUPTED_JSON,
                    "cannot parse json object");
    }
    else {
      generateError(HttpResponse::BAD,
                    TRI_ERROR_HTTP_CORRUPTED_JSON,
                    errmsg);

      TRI_FreeString(TRI_CORE_MEM_ZONE, errmsg);
    }

    return nullptr;
  }

  TRI_ASSERT(errmsg == nullptr);

  if (! TRI_IsArrayJson(json)) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    generateError(HttpResponse::BAD, TRI_ERROR_HTTP_BAD_PARAMETER,
                  "body of request must be a JSON array");
    return nullptr;
  }

  return json;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End: