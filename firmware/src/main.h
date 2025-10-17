struct Status {
    bool wifi;
    bool www;
    bool api;
    bool fetch;
    unsigned long timestamp;    
};



struct ApiInfo {
    const char* name;   // e.g., "Weather API"
    const char* url;    // e.g., "https://api.example.com/weather"
    int rateLimit;      // e.g., calls per minute
};

ApiInfo apiList[] = {
    {"Weather API", "https://api.example.com/weather", 60},
    {"Stock API",   "https://api.example.com/stocks",  30},
    {"News API",    "https://api.example.com/news",    10}
};

const int apiCount = sizeof(apiList) / sizeof(apiList[0]);


ApiInfo* selectApiByIndex(int index) {
    if (index >= 0 && index < apiCount) {
        return &apiList[index];
    }
    return nullptr; // Invalid index
}