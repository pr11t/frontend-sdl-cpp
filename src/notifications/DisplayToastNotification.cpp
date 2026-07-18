#include "DisplayToastNotification.h"

std::string DisplayToastNotification::name() const
{
    return "DisplayToastNotification";
}

const ToastOptions& DisplayToastNotification::Options() const
{
    return _options;
}
