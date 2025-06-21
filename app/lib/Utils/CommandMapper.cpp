#include "CommandMapper.h"
#include "app.h"  // For access to updateManualControlTime()

namespace Utils {

CommandMapper::CommandMapper(Utils::Logger *logger) {
    _logger = logger;
    initCommandHandlers();
}

void CommandMapper::initCommandHandlers() {
    // _commandHandlers["LOOK_AROUND"] = [this](const String& param) -> bool {
    //     if (_screen && _screen->getFace()) {
    //         _screen->getFace()->LookLeft();
    //         delay(500);
    //         _screen->getFace()->LookRight();
    //         delay(500);
    //         _screen->getFace()->LookTop();
    //         delay(500);
    //         _screen->getFace()->LookBottom();
    //         delay(500);
    //         _screen->getFace()->LookFront();
    //         _logger->debug("Looked around");
    //         return true;
    //     }
    //     return false;
    // };
}

bool CommandMapper::executeCommand(const String& commandStr) {
    // Extract command and parameter using regex
    std::regex cmdRegex("\\[([A-Z_]+)(?:=([0-9msh]+))?\\]");
    std::cmatch matches;
    std::string cmdStrStd = commandStr.c_str();
    
    if (std::regex_match(cmdStrStd.c_str(), matches, cmdRegex)) {
        String command = String(matches[1].str().c_str());
        String parameter = matches.size() > 2 ? String(matches[2].str().c_str()) : "";
        
        _logger->debug("Executing command: " + command + (parameter.isEmpty() ? "" : " with param: " + parameter));
        
        // Look up command handler
        if (_commandHandlers.count(command.c_str()) > 0) {
            return _commandHandlers[command.c_str()](parameter);
        } else {
            _logger->warning("Unknown command: " + command);
            return false;
        }
    }
    
    _logger->warning("Invalid command format: " + commandStr);
    return false;
}

int CommandMapper::executeCommandString(const String& multiCommandStr) {
    // Extract all commands from string
    std::regex cmdRegex("\\[([A-Z_]+)(?:=([0-9msh]+))?\\]");
    std::string multiCmdStd = multiCommandStr.c_str();
    std::sregex_iterator it(multiCmdStd.begin(), multiCmdStd.end(), cmdRegex);
    std::sregex_iterator end;
    
    int successCount = 0;
    
    // Execute each command
    for (; it != end; ++it) {
        std::smatch match = *it;
        String cmdStr = match.str(0).c_str();
        
        if (executeCommand(cmdStr)) {
            successCount++;
        }
    }
    
    return successCount;
}

String CommandMapper::extractCommands(const String& gptResponse) {
    // Extract all commands from GPT response
    std::regex cmdRegex("\\[([A-Z_]+)(?:=([0-9msh]+))?\\]");
    std::string responseStd = gptResponse.c_str();
    
    std::string result;
    std::sregex_iterator it(responseStd.begin(), responseStd.end(), cmdRegex);
    std::sregex_iterator end;
    
    // Concatenate all commands
    for (; it != end; ++it) {
        std::smatch match = *it;
        result += match.str(0);
    }
    
    return String(result.c_str());
}

String CommandMapper::extractText(const String& gptResponse) {
    // Remove all commands from GPT response to get just the text
    std::regex cmdRegex("\\[([A-Z_]+)(?:=([0-9msh]+))?\\]");
    std::string responseStd = gptResponse.c_str();
    
    // Replace all commands with empty string
    std::string result = std::regex_replace(responseStd, cmdRegex, "");
    
    // Trim leading/trailing whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    
    return String(result.c_str());
}

int CommandMapper::parseTimeParam(const String& param) {
    int duration = 0;
    
    // Default if parsing fails
    if (param.isEmpty()) {
        return _defaultMoveDuration;
    }
    
    String numPart = "";
    String unit = "s";  // Default to seconds
    
    // Extract number and unit
    for (size_t i = 0; i < param.length(); i++) {
        if (isDigit(param[i])) {
            numPart += param[i];
        } else {
            unit = param.substring(i);
            break;
        }
    }
    
    // Parse number
    int value = numPart.toInt();
    if (value == 0) {
        value = 1;  // Default if parsing fails
    }
    
    // Convert to milliseconds based on unit
    if (unit.equals("s")) {
        duration = value * 1000;
    } else if (unit.equals("m")) {
        duration = value * 60000;
    } else if (unit.equals("h")) {
        duration = value * 3600000;
    } else if (unit.equals("ms")) {
        duration = value;
    } else {
        duration = value * 1000;  // Default to seconds
    }
    
    // Enforce a minimum duration to prevent very short actions
    if (duration < 100) {
        duration = 100;  // Minimum 100 ms
    }
    
    return duration;
}

} // namespace Utils
