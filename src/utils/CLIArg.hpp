#include <string>
#include <vector>

#include <Logger.hpp>

class CLIArg {
  public:
    struct ArgumentDescription {
        char        short_name;
        std::string long_name;
        bool        takes_value;
        std::string description;

        bool        set;
        std::string value;
    };

    CLIArg() { add('h', "help", false, "Display this help."); }

    void add(char short_name, const std::string& long_name, bool takes_value, const std::string& description) {
        _arguments.push_back({.short_name = short_name, .long_name = long_name, .takes_value = takes_value, .description = description});
    }
    void parse(int argc, char* argv[]);

    const ArgumentDescription& operator[](char c) { return *get_short(c); }

    const std::string& get_default_arg() const { return _default_arg; }

    void print_help() const {
        success("  [{}] Help:\n", _program_name);
        for(const ArgumentDescription& d : _arguments)
            success("    -{}  --{:8} {}\n", d.short_name, d.long_name, d.description);
    }

  private:
    std::string                      _program_name{};
    std::string                      _default_arg{};
    std::vector<ArgumentDescription> _arguments;

    ArgumentDescription* get_long(const char* arg_name) {
        for(ArgumentDescription& d : _arguments) {
            if(d.long_name == arg_name)
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

void CLIArg::parse(int argc, char* argv[]) {
    if(argc > 0)
        _program_name = argv[0];

    size_t idx = 1;
    while(idx < argc) {
        if(argv[idx][0] == '-') {
            if(argv[idx][1] == '-') {
                auto a = get_long(argv[idx] + 2);
                if(!a) {
                    warn("[CLIArg] Unknown argument '{}'.\n", argv[idx] + 2);
                } else {
                    a->set = true;
                    if(a->takes_value && idx + 1 < argc) {
                        ++idx;
                        a->value = argv[idx];
                    }
                }
            } else {
                size_t p = 1;
                while(argv[idx][p] != '\0') {
                    auto a = get_short(argv[idx][p]);
                    if(!a) {
                        warn("[CLIArg] Unknown argument '{}'.\n", argv[idx] + p);
                    } else {
                        a->set = true;
                        if(a->takes_value && idx + 1 < argc) {
                            ++idx;
                            a->value = argv[idx];
                        }
                    }
                    ++p;
                }
            }
        } else {
            _default_arg = argv[idx];
        }
        ++idx;
    }

    if(get_short('h')->set) {
        print_help();
    }
}