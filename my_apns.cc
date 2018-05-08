#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <string>
#include <iostream>

#include "curl/curl.h"

static
void dump(const char *text, int num, unsigned char *ptr, size_t size,
          char nohex)
{
  size_t i;
  size_t c;
  unsigned int width = 0x10;
 
  if(nohex)
    /* without the hex output, we can fit more on screen */ 
    width = 0x40;
 
  fprintf(stderr, "%d %s, %ld bytes (0x%lx)\n",
          num, text, (long)size, (long)size);
 
  for(i = 0; i<size; i += width) {
 
    fprintf(stderr, "%4.4lx: ", (long)i);
 
    if(!nohex) {
      /* hex not disabled, show it */ 
      for(c = 0; c < width; c++)
        if(i + c < size)
          fprintf(stderr, "%02x ", ptr[i + c]);
        else
          fputs("   ", stderr);
    }
 
    for(c = 0; (c < width) && (i + c < size); c++) {
      /* check for 0D0A; if found, skip past and start a new line of output */ 
      if(nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D &&
         ptr[i + c + 1] == 0x0A) {
        i += (c + 2 - width);
        break;
      }
      fprintf(stderr, "%c",
              (ptr[i + c] >= 0x20) && (ptr[i + c]<0x80)?ptr[i + c]:'.');
      /* check again for 0D0A, to avoid an extra \n if it's at width */ 
      if(nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D &&
         ptr[i + c + 2] == 0x0A) {
        i += (c + 3 - width);
        break;
      }
    }
    fputc('\n', stderr); /* newline */ 
  }
}
 
static
int my_trace(CURL *handle, curl_infotype type,
             char *data, size_t size,
             void *userp)
{
  char timebuf[20];
  const char *text;
//  int num = hnd2num(handle);
  int num = 0;
  static time_t epoch_offset;
  static int    known_offset;
  struct timeval tv;
  time_t secs;
  struct tm *now;

  (void)handle; /* prevent compiler warning */
  (void)userp;

  gettimeofday(&tv, NULL);
  if(!known_offset) {
    epoch_offset = time(NULL) - tv.tv_sec;
    known_offset = 1;
  }
  secs = epoch_offset + tv.tv_sec;
  now = localtime(&secs);  /* not thread safe but we don't care */
  snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d.%06ld",
           now->tm_hour, now->tm_min, now->tm_sec, (long)tv.tv_usec);

  switch(type) {
  case CURLINFO_TEXT:
    fprintf(stderr, "%s [%d] Info: %s", timebuf, num, data);
    /* FALLTHROUGH */
  default: /* in case a new one is introduced to shock us */
    return 0;

  case CURLINFO_HEADER_OUT:
    text = "=> Send header";
    break;
  case CURLINFO_DATA_OUT:
    text = "=> Send data";
    break;
  case CURLINFO_SSL_DATA_OUT:
    text = "=> Send SSL data";
    break;
  case CURLINFO_HEADER_IN:
    text = "<= Recv header";
    break;
  case CURLINFO_DATA_IN:
    text = "<= Recv data";
    break;
  case CURLINFO_SSL_DATA_IN:
    text = "<= Recv SSL data";
    break;
  }

  dump(text, num, (unsigned char *)data, size, 1);
  return 0;
}

int main(int argc, char* argv[]) {
  std::string token = "348f762dffc0cd73b91958aea7c2d096dbba6560eb0bebc9a5254d75823e8b7b";
  std::string cert_file = "/home/wuxiaofei-xy/for_sample/cert/apns-pro.pem";
  std::string host = "api.push.apple.com";
  //std::string token = "48dfd4ac621749f79722cf119d2bb20674b2f3e489f0fe8f7b75a6e9fb31890f";
  //std::string cert_file = "/home/wuxiaofei-xy/for_sample/cert/apns-pro.pem";
  //std::string host = "api.development.push.apple.com";

  std::string path = "/3/device/" + token;
  std::string url = "https://" + host + path;
  std::string msg = "{\"aps\":{\"alert\":\"hi ninjacn\",\"badge\":42}}";
 
  CURL* curl = curl_easy_init(); 
  if (!curl) {
    fprintf(stderr, "init curl failed\n", strerror(errno));
    return -1;
  }
  
  std::cout << url << std::endl;
  curl_easy_setopt(curl, CURLOPT_URL, url.data());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, msg.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, msg.size());
  curl_easy_setopt(curl, CURLOPT_SSLCERT, cert_file.data());
  curl_easy_setopt(curl, CURLOPT_SSLCERTPASSWD, "abc123");

  curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "apns-topic: com.webteam.imsdk");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  //CURLcode ret = curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
  CURLcode ret = curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2);
  fprintf(stderr, "version res: %u\n", ret);
  //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  //curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, my_trace);
  
  for (int32_t i = 0; i != 100; ++i) {
    if (curl_easy_perform(curl) != CURLE_OK) {
      fprintf(stderr, "curl easy perform error: %s\n", strerror(errno));
      return -1;
    }
  }
  curl_easy_cleanup(curl);
  return 0; 
}
