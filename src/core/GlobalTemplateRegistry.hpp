#pragma once

class GlobalTemplateRegistry {
    inline static GlobalTemplateRegistry& instance() {
        static GlobalTemplateRegistry gtr;
        return gtr;
    }
};
