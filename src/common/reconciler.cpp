/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "reconciler.h"

namespace Flotsam {

Reconciler::Result Reconciler::reconcile(const Input &input)
{
    Result result;
    if (input.remote && input.remote->tombstone) {
        if (input.base && input.base->tombstone && input.local) {
            result.action = UploadLocal;
            result.reason = QStringLiteral("The network was explicitly re-added after deletion");
        } else {
            result.action = input.local ? RemoveLocal : AdoptMatching;
            result.reason = QStringLiteral("A remote tombstone wins on every device");
        }
        return result;
    }
    if (input.locallyBlocked) {
        result.action = Blocked;
        result.reason = input.local
                ? QStringLiteral("The network is kept only on this device")
                : QStringLiteral("The network is kept off this device");
        return result;
    }
    if (!input.base) {
        if (input.local && input.remote) {
            result.action = NetworkRecord::sameContent(*input.local, *input.remote)
                    ? AdoptMatching : Conflict;
            result.reason = result.action == AdoptMatching
                    ? QStringLiteral("First-seen records match")
                    : QStringLiteral("First-seen records differ");
        } else if (input.local) {
            result.action = input.approvedLocal ? UploadLocal : NewLocalDecision;
            result.reason = QStringLiteral("A network was learned on this device");
        } else if (input.remote) {
            result.action = ApplyRemote;
            result.reason = QStringLiteral("A network exists only in Nextcloud");
        }
        return result;
    }

    if (!input.remote) {
        result.action = RemoteMissing;
        result.reason = QStringLiteral("A previously synchronized remote record is missing");
        return result;
    }
    if (!input.local) {
        if (input.base->tombstone) {
            result.action = AdoptMatching;
            result.reason = QStringLiteral("The tombstone is already applied locally");
        } else {
            result.action = ForgottenDecision;
            result.reason = QStringLiteral("The network was forgotten in Sailfish Settings");
        }
        return result;
    }

    const bool localAtBase = NetworkRecord::sameContent(*input.local, *input.base);
    const bool remoteAtBase = NetworkRecord::sameContent(*input.remote, *input.base);
    const bool localAtRemote = NetworkRecord::sameContent(*input.local, *input.remote);

    if (localAtRemote) {
        result.action = NoOp;
        result.reason = QStringLiteral("Local and remote contents match");
    } else if (localAtBase && !remoteAtBase) {
        result.action = ApplyRemote;
        result.reason = QStringLiteral("Only Nextcloud changed");
    } else if (!localAtBase && remoteAtBase) {
        result.action = UploadLocal;
        result.reason = QStringLiteral("Only this device changed");
    } else {
        result.action = Conflict;
        result.reason = QStringLiteral("Both sides changed from their common base");
    }
    return result;
}

QString Reconciler::actionName(Action action)
{
    switch (action) {
    case NoOp: return QStringLiteral("none");
    case AdoptMatching: return QStringLiteral("adopt");
    case UploadLocal: return QStringLiteral("upload");
    case ApplyRemote: return QStringLiteral("apply");
    case RemoveLocal: return QStringLiteral("remove");
    case NewLocalDecision: return QStringLiteral("pending");
    case Conflict: return QStringLiteral("conflict");
    case ForgottenDecision: return QStringLiteral("forgotten");
    case Blocked: return QStringLiteral("blocked");
    case RemoteMissing: return QStringLiteral("remote-missing");
    }
    return QStringLiteral("unknown");
}

}
