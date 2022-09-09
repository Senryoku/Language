#include <string>
#include <vector>

#include <Logger.hpp>

class CLIArg {
  public:
    struct ArgumentDescription {
        const char         short_name;
        const std::string  long_name;
        const unsigned int min_values;
        const unsigned int max_values; // Note: Parsing will stop at the next option (string starting by '-')
        const std::string  description;

        bool                     set = false;
        std::vector<std::string> values;

        bool               has_value() const { return !values.empty(); }
        const std::string& value() const {
            assert(max_values == 1);
            return values.front();
        }
    };

    CLIArg() { add('h', "help", 0, 0, "Display this help."); }

    void add(char short_name, const std::string& long_name, unsigned int min_values, unsigned int max_values, const std::string& description) {
        _arguments.push_back({.short_name = short_name, .long_name = long_name, .min_values = min_values, .max_values = max_values, .description = description});
    }
    bool parse(int argc, char* argv[]);

    const ArgumentDescription& operator[](char c) const { return *get_short(c); }
    const ArgumentDescription& operator[](const std::string& c) const { return *get_long(c.c_str()); }

    bool                            has_default_args() const { return !_default_args.empty(); }
    const std::string&              get_default_arg() const { return _default_args[0]; }
    const std::vector<std::string>& get_default_args() const { return _default_args; }

    void print_help() const {
        print("  [{}] Help:\n", _program_name);
        for(const ArgumentDescription& d : _arguments)
            print("    -{}  --{:8} {}\n", d.short_name, d.long_name, d.description);
    }

  private:
    std::string                      _program_name;
    std::vector<std::string>         _default_args;
    std::vector<ArgumentDescription> _arguments;

    const ArgumentDescription* get_long(const char* arg_name) const {
        for(const ArgumentDescription& d : _arguments) {
            if(d.long_name == arg_name)
                return &d;
        }
        return nullptr;
    }

    ArgumentDescription* get_long(const char* arg_name) {
        for(ArgumentDescription& d : _arguments) {
            if(d.long_name == arg_name)
                return &d;
        }
        return nullptr;
    }

    const ArgumentDescription* get_short(const char arg_name) const {
        for(const ArgumentDescription& d : _arguments) {
            if(d.short_name == arg_name)
                return &d;
        }
        return nullptr;
    }

    ArgumentDescription* get_short(const char arg_name) {
        for(ArgumentDescription& d : _arguments) {
            if(d.short_name == arg_name)
                return &d;
        }
        return nullptr;
    }
};

bool CLIArg::parse(int argc, char* argv[]) {
    if(argc > 0)
        _program_name = argv[0];

    size_t idx = 1;
    while(idx < argc) {
        if(argv[idx][0] == '-') {
            if(argv[idx][1] == '-') {
                auto a = get_long(argv[idx] + 2);
                if(!a) {
                    warn("[CLIArg] Unknown argument '{}'.\n", argv[idx] + 2);
                    ++idx;
                } else {
                    a->set = true;
                    ++idx;
                    if(a->max_values > 0) {
                        while(idx < argc && a->values.size() < a->max_values && argv[idx][0] != '-') {
                            a->values.push_back(argv[idx]);
                            ++idx;
                        }
                        if(a->values.size() < a->min_values) {
                            error("[CLIArg] Option {} takes at least {} arguments, {} provided (Max: {}).\n", argv[idx] + 2, a->min_values, a->values.size(), a->max_values);
                            return false;
                        }
                    }
                }
            } else {
                size_t p = 1;
                size_t next_arg = idx + 1;
                // Each character is an option
                while(argv[idx][p] != '\0') {
                    auto a = get_short(argv[idx][p]);
                    if(!a) {
                        warn("[CLIArg] Unknown argument '{}'.\n", argv[idx] + p);
                    } else {
                        a->set = true;
                        if(a->max_values > 0) {
                            while(next_arg < argc && a->values.size() < a->max_values && argv[next_arg][0] != '-') {
                                a->values.push_back(argv[next_arg]);
                                ++next_arg;
                            }
                        }
                    }
                    ++p;
                }
                idx = next_arg;
            }
        } else {
            _default_args.push_back(argv[idx]);
            ++idx;
        }
    }

    if(get_short('h')->set) {
        print_help();
        exit(0);
    }
    return true;
}
