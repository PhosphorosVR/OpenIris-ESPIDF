#pragma once
#ifndef COMMAND_RESULT
#define COMMAND_RESULT

#include <algorithm>
#include <format>
#include <nlohmann-json.hpp>
#include <string>

using json = nlohmann::json;

class CommandResult
{
   public:
    enum class Status
    {
        SUCCESS,
        FAILURE,
    };

   private:
    nlohmann::json data;
    Status status;

   public:
    CommandResult(nlohmann::json data, const Status status) : data(data), status(status) {}

    bool isSuccess() const
    {
        return status == Status::SUCCESS;
    }

    static CommandResult getSuccessResult(nlohmann::json message)
    {
        return CommandResult(message, Status::SUCCESS);
    }

    static CommandResult getErrorResult(nlohmann::json message)
    {
        return CommandResult(message, Status::FAILURE);
    }

    nlohmann::json getData() const
    {
        return this->data;
    }
};

void to_json(nlohmann::json& j, const CommandResult& result);
void from_json(const nlohmann::json& j, CommandResult& result);

class CommandManagerResponse
{
   private:
    nlohmann::json data;

   public:
    CommandManagerResponse(nlohmann::json data) : data(data) {}
    nlohmann::json getData() const
    {
        return this->data;
    }
};

void to_json(nlohmann::json& j, const CommandManagerResponse& result);
void from_json(const nlohmann::json& j, CommandManagerResponse& result);

#endif