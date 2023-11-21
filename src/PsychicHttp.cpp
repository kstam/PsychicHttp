#include "PsychicHttp.h"

/*************************************/
/*  PsychicHttpServer                */
/*************************************/

PsychicHttpServer::PsychicHttpServer()
{
  //some configs
  this->config = HTTPD_DEFAULT_CONFIG();

  this->config.global_user_ctx = this;
  this->config.global_user_ctx_free_fn = this->destroy;

  this->config.max_uri_handlers = 10;
  this->config.stack_size = 10000;

  this->defaultEndpoint = PsychicHttpServerEndpoint(this, HTTP_GET);
}

PsychicHttpServer::~PsychicHttpServer()
{

}

void PsychicHttpServer::destroy(void *ctx)
{
  PsychicHttpServer *temp = (PsychicHttpServer *)ctx;
  delete temp;
}

bool PsychicHttpServer::begin(uint16_t port)
{
  this->config.server_port = port;

  /* Start the httpd server */
  if (httpd_start(&this->server, &this->config) != ESP_OK)
    return false;
  else
    return true;
}

bool PsychicHttpServer::begin(uint16_t port, const char *cert, const char *private_key)
{
  this->config.server_port = port;

  /* Start the httpd server */
  if (httpd_start(&this->server, &this->config) == ESP_OK)
    return false;
  else
    return true;
}

void PsychicHttpServer::stop()
{
  httpd_stop(this->server);
}

PsychicHttpServerEndpoint *PsychicHttpServer::on(const char* uri)
{
  return on(uri, HTTP_GET);
}

PsychicHttpServerEndpoint *PsychicHttpServer::on(const char* uri, PsychicHttpRequestHandler onRequest)
{
  return on(uri, HTTP_GET)->onRequest(onRequest);
}

PsychicHttpServerEndpoint *PsychicHttpServer::on(const char* uri, http_method method, PsychicHttpRequestHandler onRequest)
{
  return on(uri, method)->onRequest(onRequest);
}

PsychicHttpServerEndpoint *PsychicHttpServer::on(const char* uri, http_method method)
{
  PsychicHttpServerEndpoint *handler = new PsychicHttpServerEndpoint(this, method);
  
  // URI handler structure
  httpd_uri_t my_uri {
    .uri      = uri,
    .method   = method,
    .handler  = handler->requestHandler,
    .user_ctx = handler
  };

  // Register handler
  if (httpd_register_uri_handler(this->server, &my_uri) != ESP_OK) {
    Serial.println("PsychicHttp add endpoint failed");
  }

  return handler;
}

PsychicHttpServerEndpoint *PsychicHttpServer::websocket(const char* uri)
{
  PsychicHttpServerEndpoint *handler = new PsychicHttpServerEndpoint(this, HTTP_GET);
  
  // URI handler structure
  httpd_uri_t my_uri {
    .uri      = uri,
    .method   = HTTP_GET,
    .handler  = handler->websocketHandler,
    .user_ctx = handler,
    .is_websocket = true
  };

  // Register handler
  if (httpd_register_uri_handler(this->server, &my_uri) != ESP_OK) {
    Serial.println("PsychicHttp add websocket failed");
  }

  //TODO: figure out how to make a close handler???
  //esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, handler->closeHandler, this);

  return handler;
}

void PsychicHttpServer::onNotFound(PsychicHttpRequestHandler fn)
{
  this->defaultEndpoint.onRequest(fn);

  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, this->defaultEndpoint.notFoundHandler);
}

void PsychicHttpServer::sendAll(httpd_ws_frame_t * ws_pkt)
{
  size_t max_clients = this->config.max_open_sockets;
  size_t clients = max_clients;
  int    client_fds[max_clients];

  if (httpd_get_client_list(this->server, &clients, client_fds) == ESP_OK)
  {
    for (size_t i=0; i < clients; ++i)
    {
      int sock = client_fds[i];
      if (httpd_ws_get_fd_info(this->server, sock) == HTTPD_WS_CLIENT_WEBSOCKET)
      {
        ESP_LOGI(TAG, "Active client (fd=%d) -> sending async message", sock);

        // async_resp_arg *resp_arg = (async_resp_arg *)malloc(sizeof(struct async_resp_arg));
        // resp_arg->hd = this->server;
        // resp_arg->fd = sock;
        // resp_arg->ws_pkt = ws_pkt;

        httpd_ws_send_data(this->server, sock, ws_pkt);
        //httpd_ws_send_frame_async(this->server, sock, ws_pkt);

        // if (httpd_queue_work(resp_arg->hd, this->sendAsync, resp_arg) != ESP_OK) {
        //   ESP_LOGE(TAG, "httpd_queue_work failed!");
        //   break;
        // }
      }
    }
  } else {
      ESP_LOGE(TAG, "httpd_get_client_list failed!");
      return;
  }
}

void PsychicHttpServer::sendAll(httpd_ws_type_t op, const void *data, size_t len)
{
  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

  ws_pkt.payload = (uint8_t*)data;
  ws_pkt.len = len;
  ws_pkt.type = op;

  this->sendAll(&ws_pkt);
}

void PsychicHttpServer::sendAll(const char *buf)
{
  this->sendAll(HTTPD_WS_TYPE_TEXT, buf, strlen(buf));
}

/*************************************/
/*  PsychicHttpServerEndpoint        */
/*************************************/

PsychicHttpServerEndpoint::PsychicHttpServerEndpoint() :
  server(NULL),
  method(HTTP_GET),
  request(NULL),
  //upload(NULL),
  close(NULL),
  wsConnect(NULL),
  wsFrame(NULL)
{
}

PsychicHttpServerEndpoint::PsychicHttpServerEndpoint(PsychicHttpServer *server, http_method method) :
  server(server),
  method(method),
  request(NULL),
  //upload(NULL),
  close(NULL),
  wsConnect(NULL),
  wsFrame(NULL)
{
}

PsychicHttpServerEndpoint * PsychicHttpServerEndpoint::onRequest(PsychicHttpRequestHandler handler) {
  this->request = handler;
  return this;
}

// PsychicHttpServerEndpoint * PsychicHttpServerEndpoint::onUpload(PsychicHttpUploadHandler handler) {
//   this->upload = handler;
//   return this;
// }

PsychicHttpServerEndpoint * PsychicHttpServerEndpoint::onConnect(PsychicHttpWebSocketConnectionHandler handler) {
  this->wsConnect = handler;
  return this;
}

PsychicHttpServerEndpoint * PsychicHttpServerEndpoint::onFrame(PsychicHttpWebSocketFrameHandler handler) {
  this->wsFrame = handler;
  return this;
}

PsychicHttpServerEndpoint * PsychicHttpServerEndpoint::onClose(PsychicHttpRequestHandler handler) {
  Serial.println("WARNING: onClose not yet implemented");
  this->close = handler;
  return this;
}

esp_err_t PsychicHttpServerEndpoint::requestHandler(httpd_req_t *req)
{
  PsychicHttpServerEndpoint *self = (PsychicHttpServerEndpoint *)req->user_ctx;
  PsychicHttpServerRequest* request = new PsychicHttpServerRequest(self->server, req);
  esp_err_t err = self->request(request);
  delete request;

  return err;
}

esp_err_t PsychicHttpServerEndpoint::notFoundHandler(httpd_req_t *req, httpd_err_code_t err)
{
  PsychicHttpServer *server = (PsychicHttpServer*)httpd_get_global_user_ctx(req->handle);
  PsychicHttpServerRequest* request = new PsychicHttpServerRequest(server, req);
  esp_err_t result = server->defaultEndpoint.request(request);
  delete request;

  return result;
}

void PsychicHttpServerEndpoint::closeHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  // PsychicHttpServerEndpoint *self = (PsychicHttpServerEndpoint *)req->user_ctx;
  // PsychicHttpServerRequest* request = new PsychicHttpServerRequest(self->server, req);
  // esp_err_t err = self->request(request);
  // delete request;

  // return err;
}

esp_err_t PsychicHttpServerEndpoint::websocketHandler(httpd_req_t *req)
{
  PsychicHttpServerEndpoint *self = (PsychicHttpServerEndpoint *)req->user_ctx;
  PsychicHttpWebSocketConnection connection(self->server, req);

  // beginning of the ws URI handler
  if (req->method == HTTP_GET) {

      //connection hook?
      if (self->wsConnect != NULL)
        self->wsConnect(&connection);
      return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  uint8_t *buf = NULL;
  /* Set max_len = 0 to get the frame len */
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
      return ret;
  }
  ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
  if (ws_pkt.len) {
    /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
    buf = (uint8_t*) calloc(1, ws_pkt.len + 1);
    if (buf == NULL) {
      ESP_LOGE(TAG, "Failed to calloc memory for buf");
      return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;
    /* Set max_len = ws_pkt.len to get the frame payload */
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
      free(buf);
      return ret;
    }
    ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
  }
  
  // If it was a PONG, update the keep-alive
  // if (ws_pkt.type == HTTPD_WS_TYPE_PONG) {
  //     ESP_LOGD(TAG, "Received PONG message");
  //     free(buf);
  //     return wss_keep_alive_client_is_active(httpd_get_global_user_ctx(req->handle), httpd_req_to_sockfd(req));
  // }

  //okay, call our frame handler.
  if (self->wsFrame != NULL)
    ret = self->wsFrame(&connection, &ws_pkt);

  free(buf);

  return ret;
 
  // PsychicHttpServerRequest* request = new PsychicHttpServerRequest(self->server, req);
  // esp_err_t err = self->request(request);
  // delete request;

  // return err;
}

/*************************************/
/*  PsychicHttpServerRequest         */
/*************************************/

PsychicHttpServerRequest::PsychicHttpServerRequest(PsychicHttpServer *server, httpd_req_t *req) :
  _server(server),
  _req(req),
  _response(NULL)
{
  //handle our session data
  if (req->sess_ctx != NULL)
    this->_session = (SessionData *)req->sess_ctx;
  else
  {
    this->_session = new SessionData();
    req->sess_ctx = this->_session;
  }
  req->free_ctx = this->freeSession;  

  this->loadBody();
}

PsychicHttpServerRequest::~PsychicHttpServerRequest()
{
  if(_response) {
    delete _response;
    _response = NULL;
  }
}

void PsychicHttpServerRequest::freeSession(void *ctx)
{
  if (ctx != NULL)
  {
    SessionData *session = (SessionData*)ctx;
    delete session;
  }
}

void PsychicHttpServerRequest::loadBody()
{
  this->_body = String();

  /* Get header value string length and allocate memory for length + 1,
    * extra byte for null termination */
  if (this->_req->content_len > 0)
  {
    char buf[this->_req->content_len+1];

    int ret = httpd_req_recv(this->_req, buf, this->_req->content_len);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
      /* Check if timeout occurred */
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        /* In case of timeout one can choose to retry calling
        * httpd_req_recv(), but to keep it simple, here we
        * respond with an Http 408 (Request Timeout) error */
        //httpd_resp_send_408(this->_req);
        TRACE();
      }
      /* In case of error, returning ESP_FAIL will
      * ensure that the underlying socket is closed */
      //TODO: how do we handle returning values from the request?
      //return ESP_FAIL;
      TRACE();
      //return;
    }
    buf[this->_req->content_len] = '\0';

    this->_body.concat(buf);
  }
}

http_method PsychicHttpServerRequest::method() {
  return (http_method)this->_req->method;
}

String PsychicHttpServerRequest::methodStr() {
  return String(http_method_str((http_method)this->_req->method));
}

String PsychicHttpServerRequest::uri() {
  return String(this->_req->uri);
}

String PsychicHttpServerRequest::queryString() {
  size_t query_len = httpd_req_get_url_query_len(this->_req);

  //if we've got one, allocated it and load it
  if (query_len)
  {
    char query[query_len+1];
    httpd_req_get_url_query_str(this->_req, query, sizeof(query));

    return String(query);
  }
  else
    return String();
}

String PsychicHttpServerRequest::headers(const char *name)
{
  return this->header(name);
}

String PsychicHttpServerRequest::header(const char *name)
{
  size_t header_len = httpd_req_get_hdr_value_len(this->_req, name);

  //if we've got one, allocated it and load it
  if (header_len)
  {
    char header[header_len+1];
    httpd_req_get_hdr_value_str(this->_req, name, header, sizeof(header));
    return String(header);
  }
  else
    return String();
}

bool PsychicHttpServerRequest::hasHeader(const char *name)
{
  return httpd_req_get_hdr_value_len(this->_req, name) > 0;
}

String PsychicHttpServerRequest::host() {
  return String(this->header("Host"));
}

String PsychicHttpServerRequest::contentType() {
  return header("Content-Type");
}

size_t PsychicHttpServerRequest::contentLength() {
  return this->_req->content_len;
}

String PsychicHttpServerRequest::body()
{
  return this->_body;
}

void PsychicHttpServerRequest::redirect(const char *url)
{
  PsychicHttpServerResponse *response = this->beginResponse();

  response->setCode(301);
  response->addHeader("Location", url);
  
  this->send(response);
}


bool PsychicHttpServerRequest::hasCookie(const char *key)
{
  char cookie[MAX_COOKIE_SIZE];
  size_t cookieSize = MAX_COOKIE_SIZE;
  esp_err_t err = httpd_req_get_cookie_val(this->_req, key, cookie, &cookieSize);

  //did we get anything?
  if (err == ESP_OK)
    return true;
  else if (err == ESP_ERR_HTTPD_RESULT_TRUNC)
    Serial.printf("ERROR: cookie too large (%d bytes).\n", cookieSize);

  return false;
}

String PsychicHttpServerRequest::getCookie(const char *key)
{
  char cookie[MAX_COOKIE_SIZE];
  size_t cookieSize = MAX_COOKIE_SIZE;
  esp_err_t err = httpd_req_get_cookie_val(this->_req, key, cookie, &cookieSize);

  //did we get anything?
  if (err == ESP_OK)
    return String(cookie);
  else
    return "";
}

bool PsychicHttpServerRequest::hasParam(const char *key)
{
  String query = this->queryString();
  char value[query.length()];
  esp_err_t err = httpd_query_key_value(query.c_str(), key, value, query.length());

  //did we get anything?
  if (err == ESP_OK || err == ESP_ERR_HTTPD_RESULT_TRUNC)
    return true;
  else
    return false;
}

esp_err_t PsychicHttpServerRequest::getParam(const char *key, char *value)
{
  String query = this->queryString();

  return httpd_query_key_value(query.c_str(), key, value, query.length());
}

String PsychicHttpServerRequest::getParam(const char *key)
{
  String ret;

  String query = this->queryString();
  char value[query.length()];
  esp_err_t err = this->getParam(key, value);
  ret.concat(value);

  return ret;
}

bool PsychicHttpServerRequest::hasSessionKey(String key)
{
  return this->_session->find(key) != this->_session->end();
}

String PsychicHttpServerRequest::getSessionKey(String key)
{
  auto it = this->_session->find(key);
  if (it != this->_session->end())
    return it->second;
  else
    return "";
}

void PsychicHttpServerRequest::setSessionKey(String key, String value)
{
  this->_session->insert(std::pair<String, String>(key, value));
}

static String md5str(String &in){
  MD5Builder md5 = MD5Builder();
  md5.begin();
  md5.add(in);
  md5.calculate();
  return md5.toString();
}

bool PsychicHttpServerRequest::authenticate(const char * username, const char * password)
{
  if(hasHeader("Authorization"))
  {
    String authReq = header("Authorization");
    if(authReq.startsWith("Basic")){
      authReq = authReq.substring(6);
      authReq.trim();
      char toencodeLen = strlen(username)+strlen(password)+1;
      char *toencode = new char[toencodeLen + 1];
      if(toencode == NULL){
        authReq = "";
        return false;
      }
      char *encoded = new char[base64_encode_expected_len(toencodeLen)+1];
      if(encoded == NULL){
        authReq = "";
        delete[] toencode;
        return false;
      }
      sprintf(toencode, "%s:%s", username, password);
      if(base64_encode_chars(toencode, toencodeLen, encoded) > 0 && authReq.equalsConstantTime(encoded)) {
        authReq = "";
        delete[] toencode;
        delete[] encoded;
        return true;
      }
      delete[] toencode;
      delete[] encoded;
    }
    else if(authReq.startsWith(F("Digest")))
    {
      authReq = authReq.substring(7);
      String _username = _extractParam(authReq,F("username=\""),'\"');
      if(!_username.length() || _username != String(username)) {
        authReq = "";
        return false;
      }
      // extracting required parameters for RFC 2069 simpler Digest
      String _realm    = _extractParam(authReq, F("realm=\""),'\"');
      String _nonce    = _extractParam(authReq, F("nonce=\""),'\"');
      String _uri      = _extractParam(authReq, F("uri=\""),'\"');
      String _resp = _extractParam(authReq, F("response=\""),'\"');
      String _opaque   = _extractParam(authReq, F("opaque=\""),'\"');

      if((!_realm.length()) || (!_nonce.length()) || (!_uri.length()) || (!_resp.length()) || (!_opaque.length())) {
        authReq = "";
        return false;
      }
      if((_opaque != this->getSessionKey("opaque")) || (_nonce != this->getSessionKey("nonce")) || (_realm != this->getSessionKey("realm")))
      {
        authReq = "";
        return false;
      }
      // parameters for the RFC 2617 newer Digest
      String _nc,_cnonce;
      if(authReq.indexOf("qop=auth") != -1 || authReq.indexOf("qop=\"auth\"") != -1) {
        _nc = _extractParam(authReq, F("nc="), ',');
        _cnonce = _extractParam(authReq, F("cnonce=\""),'\"');
      }
      String _H1 = md5str(String(username) + ':' + _realm + ':' + String(password));
      Serial.printf("Hash of user:realm:pass=%s\n", _H1);
      String _H2 = "";
      if(_method == HTTP_GET){
          _H2 = md5str(String(F("GET:")) + _uri);
      }else if(_method == HTTP_POST){
          _H2 = md5str(String(F("POST:")) + _uri);
      }else if(_method == HTTP_PUT){
          _H2 = md5str(String(F("PUT:")) + _uri);
      }else if(_method == HTTP_DELETE){
          _H2 = md5str(String(F("DELETE:")) + _uri);
      }else{
          _H2 = md5str(String(F("GET:")) + _uri);
      }
      Serial.printf("Hash of GET:uri=%s\n", _H2);
      String _responsecheck = "";
      if(authReq.indexOf("qop=auth") != -1 || authReq.indexOf("qop=\"auth\"") != -1) {
          _responsecheck = md5str(_H1 + ':' + _nonce + ':' + _nc + ':' + _cnonce + F(":auth:") + _H2);
      } else {
          _responsecheck = md5str(_H1 + ':' + _nonce + ':' + _H2);
      }
      Serial.printf("The Proper response=%s\n", _responsecheck);
      if(_resp == _responsecheck){
        authReq = "";
        return true;
      }
    }
    authReq = "";
  }
  return false;
}

String PsychicHttpServerRequest::_extractParam(String& authReq, const String& param, const char delimit)
{
  int _begin = authReq.indexOf(param);
  if (_begin == -1)
    return "";
  return authReq.substring(_begin+param.length(),authReq.indexOf(delimit,_begin+param.length()));
}

String PsychicHttpServerRequest::_getRandomHexString()
{
  char buffer[33];  // buffer to hold 32 Hex Digit + /0
  int i;
  for(i = 0; i < 4; i++) {
    sprintf (buffer + (i*8), "%08lx", esp_random());
  }
  return String(buffer);
}

void PsychicHttpServerRequest::requestAuthentication(HTTPAuthMethod mode, const char* realm, const String& authFailMsg)
{
  //what is thy realm, sire?
  if(realm == NULL)
    this->setSessionKey("realm", "Login Required");
  else
    this->setSessionKey("realm", realm);

  PsychicHttpServerResponse *response = this->beginResponse();
  String authStr;

  //what kind of auth?
  if(mode == BASIC_AUTH)
  {
    authStr = "Basic realm=\"" + this->getSessionKey("realm") + "\"";
    response->addHeader("WWW-Authenticate", authStr.c_str());
  }
  else
  {
    this->setSessionKey("nonce", _getRandomHexString());
    this->setSessionKey("opaque", _getRandomHexString());

    authStr = "Digest realm=\"" + this->getSessionKey("realm") + "\", qop=\"auth\", nonce=\"" + this->getSessionKey("nonce") + "\", opaque=\"" + this->getSessionKey("opaque") + "\"";

    response->addHeader("WWW-Authenticate", authStr.c_str());
  }

  this->send(401, "text/html", authFailMsg.c_str());
}

PsychicHttpServerResponse *PsychicHttpServerRequest::beginResponse()
{
  return new PsychicHttpServerResponse(this->_req);
}

void PsychicHttpServerRequest::send(PsychicHttpServerResponse *response)
{
  httpd_resp_send(this->_req, response->getContent(), response->getContentLength());

  //free() the cookie memory.
  response->freeCookies();
}

void PsychicHttpServerRequest::send(int code)
{
  PsychicHttpServerResponse *response = this->beginResponse();

  response->setCode(code);
  response->setContentType("text/plain");
  response->setContent(http_status_reason(code));
  
  this->send(response);
}

void PsychicHttpServerRequest::send(const char *content)
{
  PsychicHttpServerResponse *response = this->beginResponse();

  response->setCode(200);
  response->setContentType("text/plain");
  response->setContent(content);
  
  this->send(response);
}

void PsychicHttpServerRequest::send(int code, const char *contentType, const char *content)
{
  PsychicHttpServerResponse *response = this->beginResponse();

  response->setCode(code);
  response->setContentType(contentType);
  response->setContent(content);
  
  this->send(response);
}


/*************************************/
/*  PsychicHttpServerResponse        */
/*************************************/

PsychicHttpServerResponse::PsychicHttpServerResponse(httpd_req_t *request) :
  _req(request)
{
  this->setCode(200);
}

PsychicHttpServerResponse::~PsychicHttpServerResponse()
{
}

void PsychicHttpServerResponse::addHeader(const char *name, const char *value)
{
  httpd_resp_set_hdr(this->_req, name, value);
}

//WARNING: you need to call free() on the returned string pointer!!!
char * PsychicHttpServerResponse::setCookie(const char *name, const char *value, unsigned long max_age)
{
  String output;
  String v = urlEncode(value);
  //String v = String(value);
  output = String(name) + "=" + v;
  output += "; SameSite=Lax";
  output += "; Max-Age=" + String(max_age);

  //make our string pointer and save it.
  //unfortunately, esp-idf http doesnt copy our strings
  char * out = (char *)malloc(output.length()+1);
  strlcpy(out, output.c_str(), output.length()+1);
  this->cookies.push_back(out);

  this->addHeader("Set-Cookie", out);

  return out;
}

//free all our cookie pointers since we have to keep them around.
void PsychicHttpServerResponse::freeCookies()
{
  for (char * cookie : this->cookies)
		free(cookie);
}

void PsychicHttpServerResponse::setCode(int code)
{
  //esp-idf makes you set the whole status.
  sprintf(this->_status, "%u %s", code, http_status_reason(code));

  httpd_resp_set_status(this->_req, this->_status);
}

void PsychicHttpServerResponse::setContentType(const char *contentType)
{
  httpd_resp_set_type(this->_req, contentType);
}

void PsychicHttpServerResponse::setContent(const char *content)
{
  this->body = content;
  setContentLength(strlen(content));
}

void PsychicHttpServerResponse::setContent(const uint8_t *content, size_t len)
{
  this->body = (char *)content;

  setContentLength(len);
}

const char * PsychicHttpServerResponse::getContent()
{
  return this->body;
}

size_t PsychicHttpServerResponse::getContentLength()
{
  return this->_contentLength;
}

/*************************************/
/*  PsychicHttpWebSocketConnection   */
/*************************************/

PsychicHttpWebSocketConnection::PsychicHttpWebSocketConnection(PsychicHttpServer *server, httpd_req_t *req) :
  PsychicHttpServerRequest(server, req)
{
  this->_server = req->handle;
  this->_fd = httpd_req_to_sockfd(req);
}

PsychicHttpWebSocketConnection::~PsychicHttpWebSocketConnection()
{

}

esp_err_t PsychicHttpWebSocketConnection::send(httpd_ws_frame_t * ws_pkt)
{
  return httpd_ws_send_frame(this->_req, ws_pkt);
} 

esp_err_t PsychicHttpWebSocketConnection::send(httpd_ws_type_t op, const void *data, size_t len)
{
  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

  ws_pkt.payload = (uint8_t*)data;
  ws_pkt.len = len;
  ws_pkt.type = op;

  return this->send(&ws_pkt);
}

esp_err_t PsychicHttpWebSocketConnection::send(const char *buf)
{
  return this->send(HTTPD_WS_TYPE_TEXT, buf, strlen(buf));
}