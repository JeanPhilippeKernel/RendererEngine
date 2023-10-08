#pragma once
#include <future>

namespace Tetragrama::Helpers
{
    std::future<void> OpenFileDialogAsync(std::function<void(std::string_view)> callback);
}