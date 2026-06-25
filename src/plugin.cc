#include "rack.hpp"

rack::Plugin* pluginInstance;

extern rack::plugin::Model* modelNZZL;

void init(rack::Plugin* p) {
    pluginInstance = p;
    p->addModel(modelNZZL);
}
