#pragma once

#include "notifications/ToastOptions.h"

class ProjectMGUI;

class ToastMessage
{
public:
    ToastMessage() = delete;

    explicit ToastMessage(ToastOptions options);

    /**
     * @brief Draws the toast message and returns if it still should be kept.
     * @param lastFrameTime The time in seconds since the last frame.
     * @return True if the toast message should still be displayed, false if not.
     */
    bool Draw(float lastFrameTime);

private:
    ProjectMGUI& _gui; //!< Reference to the projectM GUI instance

    ToastOptions _options; //!< Appearance and behaviour of this toast.
    float _totalTime; //!< Initial display time, for animation progress.
    float _displayTimeLeft; //!< Remaining toast message display time in seconds.
    bool _broughtToFront{false}; //!< true if the toast window was already made topmost once.
};
