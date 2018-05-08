#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <string>
#include <iostream>
#include <vector>
#include <utility>

#include "curl/curl.h"

void LogErrMsg(CURL* curl, CURLcode code, long http_code) {
  char* url = NULL;
  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url); 
  fprintf(stderr, "curl failed, time: %llu, url: %s, curl code: %d, http code: %u\n", time(NULL), url, code, http_code);
}

void CheckMultiInfo(CURLM* mc) {
  CURLMsg* m = NULL;
  CURL* c = NULL;
  int32_t n_m;
  while (m = curl_multi_info_read(mc, &n_m)) {
    static int32_t i = 0;
    fprintf(stderr, "time: %llu, exit num: %u, left: %u\n", time(NULL), ++i, n_m);

    // currently, only CURLMSG_DONE is surpported
    if (m->msg == CURLMSG_DONE) {
      c = m->easy_handle;
      long http_code;
      if (m->data.result != CURLE_OK
          || curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK
          || http_code != 200) {
        LogErrMsg(c, m->data.result, http_code);
      }
      curl_multi_remove_handle(mc, c);
      curl_easy_cleanup(c);
    }
  }
}


int32_t SocketCB(CURL* c,
                 curl_socket_t fd,
                 int32_t act,
                 void* up,
                 void* sp) {
  if (!up) {
    return -1;
  }
  int32_t ep_fd = *reinterpret_cast<int32_t*>(up);
  struct epoll_event et;
  et.events = 0; 
  et.data.fd = fd;
  if (act == CURL_POLL_REMOVE) {
    epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd, NULL);
    return 0;
  }
  if (act & CURL_POLL_IN) {
    et.events |= EPOLLIN;
  }
  if (act & CURL_POLL_OUT) {
    et.events |= EPOLLOUT;
  }
  if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd, &et) == -1) {
    epoll_ctl(ep_fd, EPOLL_CTL_MOD, fd, &et);
  }
  return 0;
}

int32_t TimerCB(CURLM* mc,
               long tm_ms,
               void* up) {
  if (!up) {
    return -1;
  }
  if (!tm_ms) {
    int32_t lf_hd = 0;
    curl_multi_socket_action(mc, CURL_SOCKET_TIMEOUT, 0, &lf_hd);
    CheckMultiInfo(mc);
  }
  
  //fprintf(stderr, "TimerCB, tm_ms: %u\n", tm_ms);
  if (tm_ms >= 1000
      || !tm_ms) {
    tm_ms = 200;
  }
  *reinterpret_cast<uint32_t*>(up) = tm_ms;
  return 0;
}

class ApnsRequest {
#define MAX_EVENT_ONCE 1024
public:
  ApnsRequest(const std::string& cert_file,
              const std::string& cert_pwd) :
      mc_(NULL),
      cert_file_(cert_file),
      cert_pwd_(cert_pwd),
      ep_fd_(0),
      tm_ms_(1000) {
  }
  virtual ~ApnsRequest() {}
  int32_t Init();
  int32_t Deinit();
  int32_t MultiSend(const std::vector<std::pair<std::string, std::string> >& items);
    
private:
  CURLM* mc_;
  std::string cert_file_;  
  std::string cert_pwd_;
  int32_t ep_fd_;
  struct epoll_event ep_ets_[MAX_EVENT_ONCE]; 
  uint32_t tm_ms_;
};

int32_t ApnsRequest::Init() {
  if ((ep_fd_ = epoll_create(MAX_EVENT_ONCE)) == -1) {
    return -1;
  } 

  mc_ = curl_multi_init();
  if (!mc_) {
    return -1;
  }
  curl_multi_setopt(mc_, CURLMOPT_SOCKETFUNCTION, SocketCB);
  curl_multi_setopt(mc_, CURLMOPT_SOCKETDATA, &ep_fd_);
  curl_multi_setopt(mc_, CURLMOPT_TIMERFUNCTION, TimerCB);
  curl_multi_setopt(mc_, CURLMOPT_TIMERDATA, &tm_ms_);
  curl_multi_setopt(mc_, CURLMOPT_MAX_HOST_CONNECTIONS, 50);
  curl_multi_setopt(mc_, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
  curl_multi_setopt(mc_, CURLMOPT_MAX_PIPELINE_LENGTH, 30);
  return 0;
}

int32_t ApnsRequest::Deinit() {
  curl_multi_cleanup(mc_);
  if (!ep_fd_) {
    close(ep_fd_);
  }
}

int32_t ApnsRequest::MultiSend(const std::vector<std::pair<std::string, std::string> >& items) {
  CURL* c = NULL;
  for (const std::pair<std::string, std::string>& item : items) {
    if (!(c = curl_easy_init())) {
      break;
    }
    curl_easy_setopt(c, CURLOPT_URL, item.first.data());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, item.second.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, item.second.size());
    curl_easy_setopt(c, CURLOPT_SSLCERT, cert_file_.data());
    curl_easy_setopt(c, CURLOPT_SSLCERTPASSWD, cert_pwd_.data());
    //curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2);

    curl_easy_setopt(c, CURLOPT_PIPEWAIT, 1L);

    curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "apns-topic: com.webteam.imsdk");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    
    curl_multi_add_handle(mc_, c);
  }
  if (!c) {
    return -1;
  }
  int32_t left_hd, num;
  do {
    num = epoll_wait(ep_fd_, reinterpret_cast<epoll_event*>(ep_ets_), MAX_EVENT_ONCE, tm_ms_);
    //fprintf(stderr, "tm_ms: %u, num: %u, left_hd: %u\n", tm_ms_, num, left_hd);
    if (num == -1 && errno == EINTR) {
      num = 0;
    }
    if (!num) {
      curl_multi_socket_action(mc_, CURL_SOCKET_TIMEOUT, 0, &left_hd);
      CheckMultiInfo(mc_); 
      continue;
    }
    for (int32_t idx = 0; idx != num; ++idx) {
      curl_multi_socket_action(mc_, ep_ets_[idx].data.fd, 0, &left_hd);
    }
    CheckMultiInfo(mc_); 
  } while (left_hd);
  return 0;
}

int32_t main(int32_t argc, char* argv[]) {
  //std::string token = "68753c78759392cfa21c575d26522a2eda165974fd9650f540d26de430c5737a";
  //std::string cert_file = "/home/wuxiaofei-xy/tmp/cert/apns-dev.pem";
  //std::string host = "api.development.push.apple.com";
  //std::string token = "5c9c2b9b371ce7fe0f7b22b9d9e95d84aa26b421c8e554546cca7095cd1d19ec";
  //std::string token = "48dfd4ac621749f79722cf119d2bb20674b2f3e489f0fe8f7b75a6e9fb31890f";
  std::string token = "348f762dffc0cd73b91958aea7c2d096dbba6560eb0bebc9a5254d75823e8b7b";
  //std::string cert_file = "/home/wuxiaofei-xy/tmp/cert/apns-distribution.pem";
  std::string cert_file = "/home/wuxiaofei-xy/for_sample/cert/apns-pro.pem";
  std::string host = "api.push.apple.com";


  std::string path = "/3/device/" + token;
  std::string url = "https://" + host + path;
  std::string msg = "{\"aps\":{\"alert\":\"hi ninjacn\",\"badge\":42}}";
  
  ApnsRequest sender(cert_file, "abc123");
  sender.Init();

  std::vector<std::pair<std::string, std::string> > send_items0, send_items; 
  for (int32_t idx = 0; idx != 1000; ++idx) {
    send_items0.push_back(std::pair<std::string, std::string>{url, msg});
  }
  for (int32_t i=0; i != 10000; ++i) {
    sender.MultiSend(send_items0);
  }
  //sleep(1);
  sender.MultiSend(send_items0);
  //sleep(1);
  sender.MultiSend(send_items0);

  for (int32_t idx = 0; idx != 1000; ++idx) {
    send_items.push_back(std::pair<std::string, std::string>{url, msg});
  }
  for (uint32_t idx = 0; idx != 10; ++idx) {
    sender.MultiSend(send_items);
    fprintf(stdout, "%d\n", idx);
  }
  while (1) sleep(1);
  sender.Deinit();
  return 0;
}
