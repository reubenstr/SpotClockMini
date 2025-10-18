
struct Quote {
    float currentPrice;
    float yesterdayClose;
};

struct Quotes {
    Quote au; 
    Quote ag; 
    Quote pt;    
};

enum class Element
{
  AU,
  AG,
  PT,
  COUNT
};

const std::unordered_map<Element, String> apiEndpoints = {
    {Element::AU, "https://forex-data-feed.swissquote.com/public-quotes/bboquotes/instrument/XAU/USD"},
    {Element::AG, "https://forex-data-feed.swissquote.com/public-quotes/bboquotes/instrument/XAG/USD"},
    {Element::PT, "https://forex-data-feed.swissquote.com/public-quotes/bboquotes/instrument/XPT/USD"},
};

const std::map<Element, const char *> elementTextMap = {
  {Element::AU, "Au"},
  {Element::AG, "Ag"},
  {Element::PT, "Pt"},
};

Element nextElement(Element current)
{
  int next = (static_cast<int>(current) + 1) % static_cast<int>(Element::COUNT);
  return static_cast<Element>(next);
}

struct Status {
    bool wifi;
    bool www;
    bool api;
    bool fetch;
    unsigned long timestamp;    
};




