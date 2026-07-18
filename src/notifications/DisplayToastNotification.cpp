#include "DisplayToastNotification.h"

DisplayToastNotification::DisplayToastNotification(std::string toastText, float displayTime)
    : _toastText(std::move(toastText))
    , _displayTime(displayTime)
{
}

std::string DisplayToastNotification::name() const
{
    return "DisplayToastNotification";
}

const std::string& DisplayToastNotification::ToastText() const
{
    return _toastText;
}

float DisplayToastNotification::DisplayTime() const
{
    return _displayTime;
}
