#pragma once

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

namespace WindowInput
{
    inline unsigned char CommandKey(int key)
    {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        {
            return static_cast<unsigned char>('a' + key - GLFW_KEY_A);
        }
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        {
            return static_cast<unsigned char>('0' + key - GLFW_KEY_0);
        }
        switch (key)
        {
        case GLFW_KEY_SPACE:
            return ' ';
        case GLFW_KEY_TAB:
            return '\t';
        case GLFW_KEY_ESCAPE:
            return 27;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            return 13;
        default:
            return 0;
        }
    }
} // namespace WindowInput
