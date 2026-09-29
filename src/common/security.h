/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FLOTSAM_SECURITY_H
#define FLOTSAM_SECURITY_H

#include <QString>

namespace Flotsam {
namespace Security {
QString userDirectory(const QString &root);
QString helperSocket();
bool trustedDirectory(const QString &path, bool writableByGroup);
bool prepareUserDirectory(const QString &root, QString *error);
bool validHelperEndpoint();
bool initializeDaemon();
}
}
#endif
