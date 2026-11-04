#pragma once

#include <functional>
#include <utility>

#include <dispatch/dispatch.h>
#include <qcoreapplication.h>
#include <qobject.h>
#include <qpointer.h>

namespace qs::macos {

class CoalescingBackgroundReader {
public:
	explicit CoalescingBackgroundReader(const char* queueLabel)
	    : queue(dispatch_queue_create(queueLabel, DISPATCH_QUEUE_SERIAL)) {}

	template <typename Read, typename Apply>
	void request(QObject* owner, Read read, Apply apply) {
		if (this->readInFlight) {
			this->pendingRequest = [this, owner, read, apply] { this->request(owner, read, apply); };
			return;
		}

		this->readInFlight = true;
		QPointer<QObject> guardedOwner = owner;
		auto* task = new std::function<void()>([this, guardedOwner, read, apply] {
			auto result = read();
			QMetaObject::invokeMethod(
			    QCoreApplication::instance(),
			    [this, guardedOwner, apply, result = std::move(result)]() mutable {
				    if (!guardedOwner) return;
				    this->readInFlight = false;
				    apply(std::move(result));
				    if (auto nextRequest = std::exchange(this->pendingRequest, {})) nextRequest();
			    },
			    Qt::QueuedConnection
			);
		});

		dispatch_async_f(this->queue, task, [](void* context) {
			auto* queuedTask = static_cast<std::function<void()>*>(context);
			(*queuedTask)();
			delete queuedTask;
		});
	}

private:
	dispatch_queue_t queue;
	bool readInFlight = false;
	std::function<void()> pendingRequest;
};

}
