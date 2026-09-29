/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_RECONCILER_H
#define FLOTSAM_RECONCILER_H

#include "networkrecord.h"

namespace Flotsam {

class Reconciler
{
public:
    enum Action {
        NoOp,
        AdoptMatching,
        UploadLocal,
        ApplyRemote,
        RemoveLocal,
        NewLocalDecision,
        Conflict,
        ForgottenDecision,
        Blocked,
        RemoteMissing
    };

    struct Input {
        const NetworkRecord *base = nullptr;
        const NetworkRecord *local = nullptr;
        const NetworkRecord *remote = nullptr;
        bool locallyBlocked = false;
        bool approvedLocal = false;
    };

    struct Result {
        Action action = NoOp;
        QString reason;
    };

    static Result reconcile(const Input &input);
    static bool isCompletedDeletion(const NetworkRecord &record, bool presentLocally,
                                    bool outstandingWork);
    static QString actionName(Action action);
};

}

#endif
