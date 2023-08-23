#include <iostream>
#include <string>
#include "hiredis/hiredis.h"
#include <iostream>
#include <cstring>
class Redis {
private:
    redisContext* context;
public:
    Redis(const std::string& host, int port) {
        context = redisConnect(host.c_str(), port);
        if (context == NULL || context->err) {
            if (context) {
                std::cout << "Error connecting to Redis: " << context->errstr << std::endl;
                redisFree(context);
            } else {
                std::cout << "Error connecting to Redis: Can't allocate redis context" << std::endl;
            }
            throw std::runtime_error("Failed to connect to Redis");
        }
    }

    ~Redis() {
        redisFree(context);
    }

    bool set(const std::string& key, const std::string& value) {
        redisReply* reply = (redisReply*)redisCommand(context, "SET %s %s", key.c_str(), value.c_str());
        if (reply == NULL) {
            std::cout << "Failed to execute Redis command: SET" << std::endl;
            return false;
        }
        bool success = (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0);
        freeReplyObject(reply);
        return success;
    }

    std::string get(const std::string& key) {
        redisReply* reply = (redisReply*)redisCommand(context, "GET %s", key.c_str());
        if (reply == NULL) {
            std::cout << "Failed to execute Redis command: GET" << std::endl;
            return "";
        }
        std::string value = "";
        if (reply->type == REDIS_REPLY_STRING) {
            value = reply->str;
        }
        freeReplyObject(reply);
        return value;
    }
};

int main() {
    Redis redis("localhost", 6379);

    // 设置键值对
    bool success = redis.set("name", "John");
    if (success) {
        std::cout << "Set value successfully!" << std::endl;
    } else {
        std::cout << "Failed to set value." << std::endl;
    }

    // 获取键值对
    std::string value = redis.get("name");
    if (!value.empty()) {
        std::cout << "Get value: " << value << std::endl;
    } else {
        std::cout << "Failed to get value." << std::endl;
    }

    return 0;
}
