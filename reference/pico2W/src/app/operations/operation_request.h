#pragma once

#include <cstdint>

enum class OperationRequestSource : uint8_t {
    Desktop,
    Tft,
    System,
};

enum class OperationRequestType : uint8_t {
    FileList,
    FileLoad,
    FileUnload,
    FileDelete,
    UploadBegin,
    UploadAbort,
    DownloadBegin,
    DownloadAbort,
    JobStart,
    JobHold,
    JobResume,
    JobAbort,
    Jog,
    JogCancel,
    HomeAll,
    ZeroAll,
    Estop,
    Reset,
    SettingsSave,
    CalibrationSave,
};

struct OperationRequest {
    OperationRequestType type = OperationRequestType::FileList;
    OperationRequestSource source = OperationRequestSource::System;
};

enum class RequestDecisionType : uint8_t {
    AcceptNow,
    Queue,
    RejectBusy,
    RejectInvalidState,
    PreemptAndAccept,
    AbortCurrentAndAccept,
    SuppressBackgroundAndAccept,
    CoalesceWithExisting,
};

struct RequestDecision {
    RequestDecisionType type = RequestDecisionType::RejectInvalidState;
};

const char* request_decision_text(RequestDecisionType type);
