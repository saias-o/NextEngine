#include "scene/animation/AnimGraphParser.hpp"
#include "scene/animation/ClipNode.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "graphics/ResourceManager.hpp"
#include "core/Log.hpp"

#include <vector>
#include <unordered_map>
#include <cctype>

namespace ne {

enum class TokenType {
    Identifier, String, Number, 
    Equals, Colon, Arrow, 
    OpenBrace, CloseBrace, OpenParen, CloseParen,
    OpEquals, OpNotEquals, OpGreater, OpLess, OpGreaterEq, OpLessEq,
    Eof, Error
};

struct Token {
    TokenType type;
    std::string_view text;
};

class AnimLexer {
public:
    AnimLexer(std::string_view source) : source_(source), pos_(0) {}

    Token peek() {
        size_t oldPos = pos_;
        Token t = next();
        pos_ = oldPos;
        return t;
    }

    Token next() {
        skipWhitespaceAndComments();
        if (pos_ >= source_.length()) return {TokenType::Eof, ""};

        char c = source_[pos_];

        if (isalpha(c) || c == '_') {
            size_t start = pos_;
            while (pos_ < source_.length() && (isalnum(source_[pos_]) || source_[pos_] == '_')) {
                pos_++;
            }
            return {TokenType::Identifier, source_.substr(start, pos_ - start)};
        }

        if (isdigit(c) || c == '-' || c == '.') {
            size_t start = pos_;
            if (c == '-') pos_++;
            while (pos_ < source_.length() && (isdigit(source_[pos_]) || source_[pos_] == '.')) {
                pos_++;
            }
            return {TokenType::Number, source_.substr(start, pos_ - start)};
        }

        if (c == '"') {
            pos_++;
            size_t start = pos_;
            while (pos_ < source_.length() && source_[pos_] != '"') pos_++;
            Token t = {TokenType::String, source_.substr(start, pos_ - start)};
            if (pos_ < source_.length()) pos_++; // skip closing quote
            return t;
        }

        if (c == '-' && pos_ + 1 < source_.length() && source_[pos_+1] == '>') {
            pos_ += 2;
            return {TokenType::Arrow, "->"};
        }
        if (c == '=' && pos_ + 1 < source_.length() && source_[pos_+1] == '=') { pos_+=2; return {TokenType::OpEquals, "=="}; }
        if (c == '!' && pos_ + 1 < source_.length() && source_[pos_+1] == '=') { pos_+=2; return {TokenType::OpNotEquals, "!="}; }
        if (c == '>' && pos_ + 1 < source_.length() && source_[pos_+1] == '=') { pos_+=2; return {TokenType::OpGreaterEq, ">="}; }
        if (c == '<' && pos_ + 1 < source_.length() && source_[pos_+1] == '=') { pos_+=2; return {TokenType::OpLessEq, "<="}; }
        if (c == '>') { pos_++; return {TokenType::OpGreater, ">"}; }
        if (c == '<') { pos_++; return {TokenType::OpLess, "<"}; }
        if (c == '=') { pos_++; return {TokenType::Equals, "="}; }
        if (c == ':') { pos_++; return {TokenType::Colon, ":"}; }
        if (c == '{') { pos_++; return {TokenType::OpenBrace, "{"}; }
        if (c == '}') { pos_++; return {TokenType::CloseBrace, "}"}; }
        if (c == '(') { pos_++; return {TokenType::OpenParen, "("}; }
        if (c == ')') { pos_++; return {TokenType::CloseParen, ")"}; }

        pos_++;
        return {TokenType::Error, source_.substr(pos_ - 1, 1)};
    }

private:
    void skipWhitespaceAndComments() {
        while (pos_ < source_.length()) {
            char c = source_[pos_];
            if (isspace(c)) {
                pos_++;
            } else if (c == '/' && pos_ + 1 < source_.length() && source_[pos_+1] == '/') {
                while (pos_ < source_.length() && source_[pos_] != '\n') pos_++;
            } else {
                break;
            }
        }
    }

    std::string_view source_;
    size_t pos_;
};

// Parser
std::unique_ptr<AnimStateMachine> AnimGraphParser::parse(std::string_view source, ResourceManager& /*resources*/, const Rig& rig) {
    AnimLexer lexer(source);
    auto sm = std::make_unique<AnimStateMachine>();

    // Fake clips for now as requested
    static std::vector<std::unique_ptr<AnimationClip>> g_fakeClips;
    std::unordered_map<std::string, const AnimationClip*> clips;
    std::unordered_map<std::string, AnimState*> states;

    Token token = lexer.next();
    while (token.type != TokenType::Eof) {
        if (token.type == TokenType::Identifier) {
            if (token.text == "graph") {
                lexer.next(); // skip name
            } else if (token.text == "clip") {
                Token name = lexer.next();
                lexer.next(); // '='
                Token path = lexer.next(); // String
                
                std::string pathStr(path.text);
                auto clip = std::make_unique<AnimationClip>(std::string(name.text), 1.0f);
                clips[std::string(name.text)] = clip.get();
                g_fakeClips.push_back(std::move(clip));
            } else if (token.text == "state") {
                Token stateName = lexer.next();
                lexer.next(); // ':'
                
                Token nextIdent = lexer.next();
                if (nextIdent.type == TokenType::Identifier && nextIdent.text == "play") {
                    Token nodeName = lexer.next();
                    auto it = clips.find(std::string(nodeName.text));
                    if (it != clips.end()) {
                        auto clipNode = std::make_unique<ClipNode>(it->second, rig);
                        auto state = std::make_unique<AnimState>(std::string(stateName.text), std::move(clipNode));
                        states[std::string(stateName.text)] = state.get();
                        sm->addState(std::move(state));
                    } else {
                        Log::error("AnimGraph: Cannot play unknown node ", nodeName.text);
                    }
                }
            } else if (token.text == "transition") {
                Token srcState = lexer.next();
                lexer.next(); // '->'
                Token dstState = lexer.next();
                lexer.next(); // ':'
                
                Token param = lexer.next();
                Token op = lexer.next();
                Token val = lexer.next();
                
                ConditionOp conditionOp;
                if (op.type == TokenType::OpEquals) conditionOp = ConditionOp::Equals;
                else if (op.type == TokenType::OpNotEquals) conditionOp = ConditionOp::NotEquals;
                else if (op.type == TokenType::OpGreater) conditionOp = ConditionOp::Greater;
                else if (op.type == TokenType::OpLess) conditionOp = ConditionOp::Less;
                else if (op.type == TokenType::OpGreaterEq) conditionOp = ConditionOp::GreaterEquals;
                else if (op.type == TokenType::OpLessEq) conditionOp = ConditionOp::LessEquals;
                else {
                    Log::error("AnimGraph: Invalid condition operator");
                    continue;
                }

                float fval = 0.0f;
                if (val.text == "true") fval = 1.0f;
                else if (val.text == "false") fval = 0.0f;
                else fval = std::stof(std::string(val.text));

                AnimCondition cond;
                cond.paramHash = hashString(param.text);
                cond.op = conditionOp;
                cond.value = fval;

                float crossfade = 0.0f;
                Token peekToken = lexer.peek();
                if (peekToken.type == TokenType::OpenParen) {
                    lexer.next(); // '('
                    Token dur = lexer.next(); // e.g., '0.2'
                    if (dur.type == TokenType::Number) {
                        crossfade = std::stof(std::string(dur.text));
                    }
                    Token possibleS = lexer.next(); // 's'
                    if (possibleS.type == TokenType::Identifier && possibleS.text == "s") {
                        lexer.next(); // ')'
                    } else if (possibleS.type != TokenType::CloseParen) {
                        // error
                    }
                }

                AnimTransition trans;
                trans.targetState = std::string(dstState.text);
                trans.conditions.push_back(cond);
                trans.crossfadeDuration = crossfade;

                auto it = states.find(std::string(srcState.text));
                if (it != states.end()) {
                    it->second->addTransition(trans);
                } else {
                    Log::error("AnimGraph: Transition from unknown state ", srcState.text);
                }
            }
        }
        token = lexer.next();
    }

    return sm;
}

} // namespace ne
