#pragma once

#ifdef LUXON_ENET_ENABLE_METRICS
    // Use this for classes that have a `metrics_` member variable
    #define ENET_METRIC_ADD(counter, amount) this->metrics_.counter.add(amount)
    #define ENET_METRIC_SUB(counter, amount) this->metrics_.counter.sub(amount)

    // Use this if passing a specific metrics instance
    #define ENET_METRIC_ADD_EX(metrics_inst, counter, amount) (metrics_inst).counter.add(amount)
    #define ENET_METRIC_SUB_EX(metrics_inst, counter, amount) (metrics_inst).counter.sub(amount)
#else
    #define ENET_METRIC_ADD(counter, amount) do {} while(0)
    #define ENET_METRIC_SUB(counter, amount) do {} while(0)

    #define ENET_METRIC_ADD_EX(metrics_inst, counter, amount) do {} while(0)
    #define ENET_METRIC_SUB_EX(metrics_inst, counter, amount) do {} while(0)
#endif
