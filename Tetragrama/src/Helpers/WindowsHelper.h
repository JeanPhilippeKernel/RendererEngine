#pragma once
#include <future>
#include <string>

namespace Tetragrama::Helpers
{
    std::future<std::string> OpenFileDialogAsync();
}