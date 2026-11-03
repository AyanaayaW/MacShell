#include "polkit.hpp"

#include <qdebug.h>
#include <qlogging.h>

namespace qs::service::polkit {

PolkitAgent::PolkitAgent(QObject* parent): QObject(parent) {
	qInfo() << "PolkitAgent is inactive on macOS; administrator prompts are handled by macOS itself.";
}

}
