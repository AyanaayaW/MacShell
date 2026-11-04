#pragma once

#include <qcolor.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qquickitem.h>
#include <qregularexpression.h>
#include <qstring.h>
#include <qsyntaxhighlighter.h>
#include <qtextformat.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::macos::syntax {

class SyntaxDefinition: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString name READ name CONSTANT);
	Q_PROPERTY(QString translatedName READ name CONSTANT);
	Q_PROPERTY(bool isValid READ isValid CONSTANT);
	QML_ANONYMOUS;

public:
	SyntaxDefinition(QString name, bool valid, QObject* parent);

	[[nodiscard]] QString name() const { return this->mName; }
	[[nodiscard]] bool isValid() const { return this->mValid; }

private:
	QString mName;
	bool mValid;
};

class SyntaxRepository: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Repository);
	QML_SINGLETON;

public:
	explicit SyntaxRepository(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE qs::macos::syntax::SyntaxDefinition* definitionForName(const QString& name);
	Q_INVOKABLE qs::macos::syntax::SyntaxDefinition* definitionForFileName(const QString& fileName);
	Q_INVOKABLE QVariant theme(const QString& name) const { return name; }
	Q_INVOKABLE QVariant defaultTheme(int variant = 0) const;
};

class CodeHighlighter: public QSyntaxHighlighter {
public:
	explicit CodeHighlighter(QTextDocument* document);

	void configure(const QString& language, bool darkTheme);

protected:
	void highlightBlock(const QString& text) override;

private:
	struct Rule {
		QRegularExpression pattern;
		QTextCharFormat format;
	};

	QList<Rule> mRules;
	QRegularExpression mBlockCommentStart;
	QRegularExpression mBlockCommentEnd;
	QTextCharFormat mCommentFormat;
	bool mBlockComments = false;
};

class SyntaxHighlighter: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QQuickItem* textEdit READ textEdit WRITE setTextEdit NOTIFY textEditChanged);
	Q_PROPERTY(QVariant definition READ definition WRITE setDefinition NOTIFY definitionChanged);
	Q_PROPERTY(QVariant theme READ theme WRITE setTheme NOTIFY themeChanged);
	Q_PROPERTY(QVariant repository READ repository WRITE setRepository NOTIFY repositoryChanged);
	QML_ELEMENT;

public:
	explicit SyntaxHighlighter(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] QQuickItem* textEdit() const { return this->mTextEdit; }
	void setTextEdit(QQuickItem* textEdit);
	[[nodiscard]] QVariant definition() const { return this->mDefinition; }
	void setDefinition(const QVariant& definition);
	[[nodiscard]] QVariant theme() const { return this->mTheme; }
	void setTheme(const QVariant& theme);
	[[nodiscard]] QVariant repository() const { return this->mRepository; }
	void setRepository(const QVariant& repository);

signals:
	void textEditChanged();
	void definitionChanged();
	void themeChanged();
	void repositoryChanged();

private:
	void apply();
	[[nodiscard]] QString languageName() const;

	QPointer<QQuickItem> mTextEdit;
	QVariant mDefinition;
	QVariant mTheme;
	QVariant mRepository;
	QPointer<CodeHighlighter> mHighlighter;
};

}
