#include "system_mirror.hpp"

#include <qloggingcategory.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtimer.h>
#include <qvariant.h>
#include <sqlite3.h>

#include "server.hpp"

#import <AppKit/AppKit.h>

namespace qs::service::notifications {

namespace {

Q_LOGGING_CATEGORY(logSystemMirror, "quickshell.service.notifications.macos", QtInfoMsg);

constexpr int POLL_INTERVAL_MILLISECONDS = 1000;
const char* const NEW_RECORDS_QUERY =
    "select record.rec_id, app.identifier, record.data from record "
    "join app on app.app_id = record.app_id where record.rec_id > ?1 order by record.rec_id";

NSString* notificationDatabasePath() {
	return [NSHomeDirectory()
	    stringByAppendingPathComponent:@"Library/Group Containers/group.com.apple.usernoted/db2/db"];
}

QString applicationNameFor(NSString* bundleIdentifier) {
	NSURL* applicationUrl = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:bundleIdentifier];
	NSBundle* bundle = applicationUrl ? [NSBundle bundleWithURL:applicationUrl] : nil;
	NSString* name = [bundle objectForInfoDictionaryKey:@"CFBundleDisplayName"]
	              ?: [bundle objectForInfoDictionaryKey:@"CFBundleName"];
	return QString::fromNSString(name ?: bundleIdentifier);
}

QString stringField(NSDictionary* dictionary, NSString* key) {
	id value = dictionary[key];
	return [value isKindOfClass:NSString.class] ? QString::fromNSString(value) : QString();
}

class SystemNotificationMirror {
public:
	bool open() {
		if (sqlite3_open_v2(notificationDatabasePath().UTF8String, &this->database, SQLITE_OPEN_READONLY, nullptr)
		    != SQLITE_OK)
		{
			this->close();
			return false;
		}

		sqlite3_stmt* statement = nullptr;
		if (sqlite3_prepare_v2(this->database, "select max(rec_id) from record", -1, &statement, nullptr) != SQLITE_OK) {
			this->close();
			return false;
		}
		if (sqlite3_step(statement) == SQLITE_ROW) this->lastRecordId = sqlite3_column_int64(statement, 0);
		sqlite3_finalize(statement);
		return true;
	}

	void close() {
		if (this->database) sqlite3_close(this->database);
		this->database = nullptr;
	}

	void deliverNewRecords() {
		sqlite3_stmt* statement = nullptr;
		if (sqlite3_prepare_v2(this->database, NEW_RECORDS_QUERY, -1, &statement, nullptr) != SQLITE_OK) return;
		sqlite3_bind_int64(statement, 1, this->lastRecordId);

		while (sqlite3_step(statement) == SQLITE_ROW) {
			this->lastRecordId = sqlite3_column_int64(statement, 0);
			const auto* identifierText = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
			const void* recordBytes = sqlite3_column_blob(statement, 2);
			auto recordLength = sqlite3_column_bytes(statement, 2);
			if (!identifierText || !recordBytes || recordLength <= 0) continue;

			@autoreleasepool {
				this->deliverRecord(@(identifierText), [NSData dataWithBytes:recordBytes length:static_cast<NSUInteger>(recordLength)]);
			}
		}
		sqlite3_finalize(statement);
	}

private:
	static void deliverRecord(NSString* bundleIdentifier, NSData* recordData) {
		if ([bundleIdentifier isEqualToString:NSBundle.mainBundle.bundleIdentifier]) return;

		NSDictionary* record = [NSPropertyListSerialization propertyListWithData:recordData
		                                                                 options:NSPropertyListImmutable
		                                                                  format:nil
		                                                                   error:nil];
		if (![record isKindOfClass:NSDictionary.class]) return;
		NSDictionary* request = record[@"req"];
		if (![request isKindOfClass:NSDictionary.class]) return;

		auto title = stringField(request, @"titl");
		auto subtitle = stringField(request, @"subt");
		auto body = stringField(request, @"body");
		if (title.isEmpty() && body.isEmpty()) return;

		if (!subtitle.isEmpty()) body = body.isEmpty() ? subtitle : subtitle + u'\n' + body;
		body = body.toHtmlEscaped();

		auto applicationIdentifier = QString::fromNSString(bundleIdentifier);
		QVariantMap hints {{QStringLiteral("desktop-entry"), applicationIdentifier}};

		NotificationServer::instance()->Notify(
		    applicationNameFor(bundleIdentifier),
		    0,
		    applicationIdentifier,
		    title.isEmpty() ? applicationNameFor(bundleIdentifier) : title,
		    body,
		    {},
		    hints,
		    -1
		);
	}

	sqlite3* database = nullptr;
	sqlite3_int64 lastRecordId = 0;
};

}

void startSystemNotificationMirror() {
	static SystemNotificationMirror* mirror = nullptr;
	if (mirror) return;

	mirror = new SystemNotificationMirror();
	if (!mirror->open()) {
		qCInfo(logSystemMirror) << "Other apps' notifications are not mirrored: grant MacShell Full Disk Access to "
		                           "show them in your shell.";
		return;
	}

	auto* timer = new QTimer();
	QObject::connect(timer, &QTimer::timeout, [] { mirror->deliverNewRecords(); });
	timer->start(POLL_INTERVAL_MILLISECONDS);
}

}
