#ifndef JSON_H
#define JSON_H

#include <stdlib.h>

#include "Types.h"
#include "Memory.h"
#include "Strings.h"

enum json_type
{
    JSON_Null = 0,
    JSON_Bool,
    JSON_Number,
    JSON_String,
    JSON_Array,
    JSON_Object,
};

struct json_token
{
    char   *Text;
    uint32  Length;
    uint32  Type;
    uint32  Count;
    uint32  Span;
};

struct json_parser
{
    char         *At;
    char         *End;
    memory_arena *Arena;
    json_token   *Tokens;
    uint32        TokenCount;
    bool32        Error;
};

internal void JsonSkipWhitespace(json_parser *Parser)
{
    while (Parser->At < Parser->End && (*Parser->At == ' ' || *Parser->At == '\t' || *Parser->At == '\r' || *Parser->At == '\n'))
    {
        Parser->At++;
    }
}

internal char JsonPeekChar(json_parser *Parser)
{
    JsonSkipWhitespace(Parser);
    return (Parser->At < Parser->End) ? *Parser->At : 0;
}

internal bool32 JsonExpect(json_parser *Parser, char C)
{
    if (JsonPeekChar(Parser) == C)
    {
        Parser->At++;

        return true;
    }

    Parser->Error = true;

    return false;
}

internal json_token *JsonPushToken(json_parser *Parser, uint32 Type)
{
    json_token *Token = PushStruct(Parser->Arena, json_token);
    if (!Parser->Tokens)
    {
        Parser->Tokens = Token;
    }
    Assert(Token == Parser->Tokens + Parser->TokenCount);

    ZeroStruct(*Token);
    Token->Type = Type;
    Token->Text = Parser->At;

    Parser->TokenCount++;

    return Token;
}

internal void JsonParseString(json_parser *P)
{
    if (!JsonExpect(P, '"'))
    {
        return;
    }

    json_token *Token = JsonPushToken(P, JSON_String);
    char *Out = P->At;
    while (P->At < P->End && *P->At != '"')
    {
        char Ch = *P->At++;
        if (Ch == '\\' && P->At < P->End)
        {
            Ch = *P->At++;
            switch (Ch)
            {
                case 'n': Ch = '\n'; break;
                case 't': Ch = '\t'; break;
                case 'r': Ch = '\r'; break;
                case 'b': Ch = '\b'; break;
                case 'f': Ch = '\f'; break;
                case 'u':
                {
                    Ch = '?';
                    P->At = (P->End - P->At > 4) ? P->At + 4 : P->End;
                } break;
            }
        }
        *Out++ = Ch;
    }

    if (P->At >= P->End)
    {
        P->Error = true;
        return;
    }

    *Out = 0;
    Token->Length = (uint32)(Out - Token->Text);
    Token->Span   = 1;
    P->At++;
}

internal void JsonParseLiteral(json_parser *P, uint32 Type, const char *Word)
{
    json_token *Token = JsonPushToken(P, Type);
    while (*Word)
    {
        if (P->At >= P->End || *P->At != *Word)
        {
            P->Error = true;
            return;
        }
        P->At++;
        Word++;
    }

    Token->Length = (uint32)(P->At - Token->Text);
    Token->Span   = 1;
}

internal void JsonParseNumber(json_parser *P)
{
    json_token *Token = JsonPushToken(P, JSON_Number);
    while (P->At < P->End && ((*P->At >= '0' && *P->At <= '9') || *P->At == '-' || *P->At == '+' || *P->At == '.' || *P->At == 'e' || *P->At == 'E'))
    {
        P->At++;
    }

    Token->Length = (uint32)(P->At - Token->Text);
    Token->Span   = 1;
    if (!Token->Length)
    {
        P->Error = true;
    }
}

internal void JsonParseValue(json_parser *Parser)
{
    char JChar = JsonPeekChar(Parser);

    if (JChar == '{' || JChar == '[')
    {
        char Close = (JChar == '{') ? '}' : ']';

        uint32 Index = Parser->TokenCount;
        JsonPushToken(Parser, (JChar == '{') ? JSON_Object : JSON_Array);
        Parser->At++;

        while (!Parser->Error)
        {
            if (JsonPeekChar(Parser) == Close)
            {
                Parser->At++;
                break;
            }
            if (Parser->Tokens[Index].Count)
            {
                JsonExpect(Parser, ',');
            }
            if (JChar == '{')
            {
                JsonParseString(Parser);
                JsonExpect(Parser, ':');
            }
            JsonParseValue(Parser);
            Parser->Tokens[Index].Count++;
        }

        Parser->Tokens[Index].Span = Parser->TokenCount - Index;
    }
    else if (JChar == '"')
    {
        JsonParseString(Parser);
    }
    else if (JChar == 't')
    {
        JsonParseLiteral(Parser, JSON_Bool, "true");
    }
    else if (JChar == 'f')
    {
        JsonParseLiteral(Parser, JSON_Bool, "false");
    }
    else if (JChar == 'n')
    {
        JsonParseLiteral(Parser, JSON_Null, "null");
    }
    else
    {
        JsonParseNumber(Parser);
    }
}

internal json_token *JsonParse(memory_arena *Arena, void *Data, uint32 Size, uint32 *OutErrorOffset)
{
    json_parser Parser = {};
    Parser.At    = (char *)Data;
    Parser.End   = Parser.At + Size;
    Parser.Arena = Arena;

    JsonParseValue(&Parser);
    if (Parser.Error)
    {
        if (OutErrorOffset)
        {
            *OutErrorOffset = (uint32)(Parser.At - (char *)Data);
        }
        return 0;
    }

    return Parser.Tokens;
}

internal json_token *JsonGet(json_token *Object, const char *Key)
{
    if (Object && Object->Type == JSON_Object)
    {
        json_token *Token = Object + 1;
        for (uint32 i = 0; i < Object->Count; ++i)
        {
            json_token *Value = Token + 1;
            if (StringsAreEqual(Token->Text, Key))
            {
                return Value;
            }
            Token = Value + Value->Span;
        }
    }

    return 0;
}

internal json_token *JsonAt(json_token *Array, uint32 Index)
{
    if (Array && Array->Type == JSON_Array && Index < Array->Count)
    {
        json_token *Token = Array + 1;
        for (uint32 i = 0; i < Index; ++i)
        {
            Token += Token->Span;
        }
        return Token;
    }

    return 0;
}

internal uint32 JsonU32(json_token *Value, uint32 Default)
{
    return (Value && Value->Type == JSON_Number) ? (uint32)strtod(Value->Text, 0) : Default;
}

internal real32 JsonR32(json_token *Value, real32 Default)
{
    return (Value && Value->Type == JSON_Number) ? (real32)strtod(Value->Text, 0) : Default;
}

internal bool32 JsonR32Array(json_token *Array, real32 *Out, uint32 Count)
{
    if (!Array || Array->Type != JSON_Array || Array->Count != Count)
    {
        return false;
    }

    for (uint32 i = 0; i < Count; ++i)
    {
        Out[i] = JsonR32(JsonAt(Array, i), 0.0f);
    }

    return true;
}

internal char *JsonCString(json_token *Value)
{
    return (Value && Value->Type == JSON_String) ? Value->Text : 0;
}

#endif
