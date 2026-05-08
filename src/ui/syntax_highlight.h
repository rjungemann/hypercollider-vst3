#pragma once

#include "imgui.h"
#include <string>
#include <unordered_set>

namespace HCPlugin {

// SC Syntax highlighting colors (matching browser IDE palette)
namespace SyntaxColors {
    constexpr ImU32 kBackground     = IM_COL32( 30,  30,  30, 255); // #1e1e1e
    constexpr ImU32 kDefaultText    = IM_COL32(224, 224, 224, 255); // #e0e0e0
    constexpr ImU32 kKeyword        = IM_COL32( 78, 201, 176, 255); // #4ec9b0 (teal)
    constexpr ImU32 kKeywordDim     = IM_COL32( 60, 150, 130, 255); // dimmer teal
    constexpr ImU32 kNumber         = IM_COL32(181, 206, 168, 255); // #b5cea8 (sage green)
    constexpr ImU32 kString         = IM_COL32(206, 145, 120, 255); // #ce9178 (warm orange)
    constexpr ImU32 kComment        = IM_COL32(106, 153,  85, 255); // #6a9955 (muted green)
    constexpr ImU32 kSymbol         = IM_COL32(197, 134, 192, 255); // #c586c0 (mauve)
    constexpr ImU32 kError          = IM_COL32(244,  71,  71, 255); // #f44747 (red)
    constexpr ImU32 kLogNormal      = IM_COL32(224, 224, 224, 255); // #e0e0e0
    constexpr ImU32 kLogError       = IM_COL32(244,  71,  71, 255); // #f44747
} // namespace SyntaxColors

// Token types for syntax highlighting
enum class TokenType {
    Default,
    Keyword,
    KeywordDim,
    Number,
    String,
    Symbol,
    Comment,
    Error
};

// Simple SC syntax highlighter for a line of text
class SyntaxHighlighter {
public:
    struct Token {
        TokenType type;
        int start;
        int end;
    };

    static std::vector<Token> tokenizeLine(const std::string& line) {
        std::vector<Token> tokens;
        int pos = 0;
        const int len = static_cast<int>(line.size());

        while (pos < len) {
            // Skip whitespace
            if (std::isspace(static_cast<unsigned char>(line[pos]))) {
                pos++;
                continue;
            }

            // Check for comments
            if (pos + 1 < len && line[pos] == '/' && line[pos+1] == '/') {
                tokens.push_back({TokenType::Comment, pos, len});
                break;
            }
            if (pos + 1 < len && line[pos] == '/' && line[pos+1] == '*') {
                // Find end of block comment
                int end = pos + 2;
                while (end + 1 < len && !(line[end] == '*' && line[end+1] == '/')) {
                    end++;
                }
                if (end + 1 < len) end += 2;
                tokens.push_back({TokenType::Comment, pos, end});
                pos = end;
                continue;
            }

            // Check for strings
            if (line[pos] == '"') {
                int start = pos;
                pos++;
                while (pos < len && line[pos] != '"') {
                    if (line[pos] == '\\' && pos + 1 < len) pos += 2;
                    else pos++;
                }
                if (pos < len) pos++; // include closing quote
                tokens.push_back({TokenType::String, start, pos});
                continue;
            }

            // Check for symbols (single-quoted)
            if (line[pos] == '\'') {
                int start = pos;
                pos++;
                while (pos < len && line[pos] != '\'') {
                    pos++;
                }
                if (pos < len) pos++; // include closing quote
                tokens.push_back({TokenType::Symbol, start, pos});
                continue;
            }

            // Check for numbers
            if (std::isdigit(static_cast<unsigned char>(line[pos])) ||
                (line[pos] == '.' && pos + 1 < len && std::isdigit(static_cast<unsigned char>(line[pos+1])))) {
                int start = pos;
                bool hasDot = (line[pos] == '.');
                pos++;
                while (pos < len && (std::isdigit(static_cast<unsigned char>(line[pos])) || line[pos] == '.')) {
                    if (line[pos] == '.') {
                        if (hasDot) break; // second dot - not a number
                        hasDot = true;
                    }
                    pos++;
                }
                // Check for exponent
                if (pos < len && (line[pos] == 'e' || line[pos] == 'E')) {
                    pos++;
                    if (pos < len && (line[pos] == '+' || line[pos] == '-')) pos++;
                    while (pos < len && std::isdigit(static_cast<unsigned char>(line[pos]))) pos++;
                }
                tokens.push_back({TokenType::Number, start, pos});
                continue;
            }

            // Check for keywords (SC class names start with uppercase)
            if (std::isupper(static_cast<unsigned char>(line[pos]))) {
                int start = pos;
                while (pos < len && (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_')) {
                    pos++;
                }
                tokens.push_back({TokenType::Keyword, start, pos});
                continue;
            }

            // Check for SC keywords (lowercase)
            if (std::islower(static_cast<unsigned char>(line[pos]))) {
                int start = pos;
                while (pos < len && (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_')) {
                    pos++;
                }
                std::string word = line.substr(start, pos - start);
                if (isKeyword(word)) {
                    tokens.push_back({TokenType::Keyword, start, pos});
                } else if (isKeywordDim(word)) {
                    tokens.push_back({TokenType::KeywordDim, start, pos});
                }
                continue;
            }

            // Default token (operators, punctuation, etc.)
            tokens.push_back({TokenType::Default, pos, pos + 1});
            pos++;
        }

        return tokens;
    }

    static ImU32 getColor(TokenType type) {
        switch (type) {
            case TokenType::Keyword:     return SyntaxColors::kKeyword;
            case TokenType::KeywordDim:  return SyntaxColors::kKeywordDim;
            case TokenType::Number:      return SyntaxColors::kNumber;
            case TokenType::String:      return SyntaxColors::kString;
            case TokenType::Symbol:      return SyntaxColors::kSymbol;
            case TokenType::Comment:     return SyntaxColors::kComment;
            case TokenType::Error:       return SyntaxColors::kError;
            default:                    return SyntaxColors::kDefaultText;
        }
    }

private:
    static bool isKeyword(const std::string& word) {
        static const std::unordered_set<std::string> kKeywords = {
            "Synth", "SynthDef", "Env", "SinOsc", "Saw", "Pulse", "WhiteNoise",
            "LFSaw", "LFNoise", "Impulse", "Dust", "GrayNoise", "BrownNoise",
            "Play", "Record", "Sample", "Buffer", "Bus", "Group", "SynthDesc",
            "Out", "In", "InFeedback", "ReplaceOut", "AddOut", "XOut",
            "AudioControl", "Control", "Audio", "UGen", "Constant",
            "Line", "XLine", "EnvGen", "ADSR", "Decay", "Decay2",
            "FreeVerb", "Comb", "Allpass", "Delay", "Echo", "Reverb",
            "Filter", "LPF", "HPF", "BPF", "BRF", "Formant",
            "Osc", "Pulse", "VarSaw", "Blip", "FormSaw", "Crackle",
            "Gendy", "Klank", "Resonz", "F sinOsc", "FSaw"
        };
        return kKeywords.find(word) != kKeywords.end();
    }

    static bool isKeywordDim(const std::string& word) {
        static const std::unordered_set<std::string> kDimKeywords = {
            "var", "arg", "this", "true", "false", "nil", "inf",
            "if", "while", "do", "collect", "select", "reject", "detect"
        };
        return kDimKeywords.find(word) != kDimKeywords.end();
    }
};

} // namespace HCPlugin
