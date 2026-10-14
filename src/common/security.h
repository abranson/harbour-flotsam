/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FLOTSAM_SECURITY_H
#define FLOTSAM_SECURITY_H

#include <QString>

namespace Flotsam {
namespace Security {
QString userDirectory(const QString &root);
QString helperSocket();
bool trustedDirectory(const QString &path, bool writableByGroup);
QString stateDirectory();
int openStateDirectory(QString *error);
// Explicit identities permit isolated filesystem fixtures; production uses NSS.
int openPrivilegedStateDirectory(const QString &home, uint owner, uint group, QString *error);
int openPreviousStateDirectory(QString *error);
bool validHelperEndpoint();
bool initializeDaemon();
}
}
#endif
