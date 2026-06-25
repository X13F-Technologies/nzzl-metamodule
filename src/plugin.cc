#include "rack.hpp"

rack::Plugin* pluginInstance;

void init(rack::Plugin* p) {
    pluginInstance = p;
    // modules registered here as the plugin grows
}
