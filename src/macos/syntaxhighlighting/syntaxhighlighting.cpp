#include "syntaxhighlighting.hpp"

#include <qfileinfo.h>
#include <qhash.h>
#include <qquicktextdocument.h>
#include <qstringlist.h>
#include <qtextdocument.h>

namespace qs::macos::syntax {

namespace {

struct LanguageRules {
	QString displayName;
	QStringList keywords;
	QString lineComment;
	bool blockComments = false;
};

const QHash<QString, LanguageRules>& languages() {
	static const QStringList cFamilyKeywords {
	    "auto", "break", "case", "catch", "class", "const", "continue", "default", "delete", "do", "else", "enum",
	    "export", "extends", "false", "for", "function", "if", "import", "in", "instanceof", "let", "new", "null",
	    "nullptr", "private", "protected", "public", "return", "static", "struct", "super", "switch", "this", "throw",
	    "true", "try", "typeof", "var", "void", "while", "namespace", "template", "typename", "using", "virtual",
	    "override", "int", "float", "double", "bool", "char", "long", "short", "unsigned", "signed", "async", "await",
	    "yield", "interface", "implements", "package", "fn", "mut", "pub", "impl", "trait", "match", "loop", "mod",
	    "use", "crate", "self", "Self", "go", "func", "defer", "chan", "map", "type", "property", "readonly", "signal",
	};
	static const QStringList pythonKeywords {
	    "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else", "except",
	    "False", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "None", "nonlocal", "not",
	    "or", "pass", "raise", "return", "True", "try", "while", "with", "yield", "self", "print",
	};
	static const QStringList shellKeywords {
	    "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac", "in", "function",
	    "return", "local", "export", "set", "unset", "echo", "exit", "source", "alias", "readonly", "shift", "end",
	};

	static const QHash<QString, LanguageRules> table = [] {
		QHash<QString, LanguageRules> rules;
		for (const auto& name: {"c", "cpp", "c++", "csharp", "c#", "java", "javascript", "js", "typescript", "ts",
		                        "rust", "go", "kotlin", "swift", "qml", "json", "css", "scss", "dart", "zig"})
		{
			rules.insert(QString::fromLatin1(name), {QString::fromLatin1(name), cFamilyKeywords, QStringLiteral("//"), true});
		}
		for (const auto& name: {"python", "py", "ruby", "rb", "toml", "yaml", "yml", "conf", "ini"}) {
			rules.insert(QString::fromLatin1(name), {QString::fromLatin1(name), pythonKeywords, QStringLiteral("#"), false});
		}
		for (const auto& name: {"bash", "sh", "shell", "zsh", "fish", "console"}) {
			rules.insert(QString::fromLatin1(name), {QString::fromLatin1(name), shellKeywords, QStringLiteral("#"), false});
		}
		rules.insert(QStringLiteral("lua"), {QStringLiteral("Lua"), pythonKeywords, QStringLiteral("--"), false});
		rules.insert(QStringLiteral("sql"), {QStringLiteral("SQL"), {}, QStringLiteral("--"), true});
		return rules;
	}();
	return table;
}

QTextCharFormat formatWith(const QColor& color, bool bold = false, bool italic = false) {
	QTextCharFormat format;
	format.setForeground(color);
	if (bold) format.setFontWeight(QFont::Bold);
	format.setFontItalic(italic);
	return format;
}

}

SyntaxDefinition::SyntaxDefinition(QString name, bool valid, QObject* parent)
    : QObject(parent)
    , mName(std::move(name))
    , mValid(valid) {}

SyntaxDefinition* SyntaxRepository::definitionForName(const QString& name) {
	auto key = name.trimmed().toLower();
	auto known = languages().contains(key);
	return new SyntaxDefinition(known ? languages().value(key).displayName : name, known, this);
}

SyntaxDefinition* SyntaxRepository::definitionForFileName(const QString& fileName) {
	return this->definitionForName(QFileInfo(fileName).suffix());
}

QVariant SyntaxRepository::defaultTheme(int variant) const {
	return variant == 0 ? QStringLiteral("ayu Light") : QStringLiteral("Monokai");
}

CodeHighlighter::CodeHighlighter(QTextDocument* document): QSyntaxHighlighter(document) {}

void CodeHighlighter::configure(const QString& language, bool darkTheme) {
	this->mRules.clear();
	auto rules = languages().value(language.trimmed().toLower());

	auto keywordColor = darkTheme ? QColor(0xF9, 0x26, 0x72) : QColor(0xFA, 0x8D, 0x3E);
	auto stringColor = darkTheme ? QColor(0xE6, 0xDB, 0x74) : QColor(0x86, 0xB3, 0x00);
	auto numberColor = darkTheme ? QColor(0xAE, 0x81, 0xFF) : QColor(0xA3, 0x7A, 0xCC);
	auto functionColor = darkTheme ? QColor(0xA6, 0xE2, 0x2E) : QColor(0xF2, 0xAE, 0x49);
	this->mCommentFormat = formatWith(darkTheme ? QColor(0x75, 0x71, 0x5E) : QColor(0xAB, 0xB0, 0xB6), false, true);

	if (!rules.keywords.isEmpty()) {
		auto escapedKeywords = QStringList();
		for (const auto& keyword: rules.keywords) escapedKeywords.append(QRegularExpression::escape(keyword));
		this->mRules.append({QRegularExpression(QStringLiteral("\\b(") + escapedKeywords.join(u'|') + QStringLiteral(")\\b")),
		                     formatWith(keywordColor, true)});
	}
	this->mRules.append({QRegularExpression(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()")), formatWith(functionColor)});
	this->mRules.append({QRegularExpression(QStringLiteral("\\b(0x[0-9A-Fa-f]+|\\d+(\\.\\d+)?)\\b")), formatWith(numberColor)});
	this->mRules.append({QRegularExpression(QStringLiteral("\"(\\\\.|[^\"\\\\])*\"|'(\\\\.|[^'\\\\])*'|`[^`]*`")), formatWith(stringColor)});

	if (!rules.lineComment.isEmpty()) {
		this->mRules.append({QRegularExpression(QRegularExpression::escape(rules.lineComment) + QStringLiteral("[^\\n]*")), this->mCommentFormat});
	}

	this->mBlockComments = rules.blockComments;
	this->mBlockCommentStart = QRegularExpression(QStringLiteral("/\\*"));
	this->mBlockCommentEnd = QRegularExpression(QStringLiteral("\\*/"));
	this->rehighlight();
}

void CodeHighlighter::highlightBlock(const QString& text) {
	for (const auto& rule: this->mRules) {
		auto matches = rule.pattern.globalMatch(text);
		while (matches.hasNext()) {
			auto match = matches.next();
			this->setFormat(static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()), rule.format);
		}
	}

	this->setCurrentBlockState(0);
	if (!this->mBlockComments) return;

	qsizetype startIndex = this->previousBlockState() == 1 ? 0 : text.indexOf(this->mBlockCommentStart);
	while (startIndex >= 0) {
		auto endMatch = this->mBlockCommentEnd.match(text, startIndex);
		qsizetype commentLength = 0;
		if (!endMatch.hasMatch()) {
			this->setCurrentBlockState(1);
			commentLength = text.length() - startIndex;
		} else {
			commentLength = endMatch.capturedEnd() - startIndex;
		}
		this->setFormat(static_cast<int>(startIndex), static_cast<int>(commentLength), this->mCommentFormat);
		startIndex = text.indexOf(this->mBlockCommentStart, startIndex + commentLength);
	}
}

void SyntaxHighlighter::setTextEdit(QQuickItem* textEdit) {
	if (textEdit == this->mTextEdit) return;
	this->mTextEdit = textEdit;
	delete this->mHighlighter;
	this->mHighlighter = nullptr;

	if (textEdit) {
		auto* quickDocument = textEdit->property("textDocument").value<QQuickTextDocument*>();
		if (quickDocument && quickDocument->textDocument()) {
			this->mHighlighter = new CodeHighlighter(quickDocument->textDocument());
		}
	}

	emit this->textEditChanged();
	this->apply();
}

void SyntaxHighlighter::setDefinition(const QVariant& definition) {
	this->mDefinition = definition;
	emit this->definitionChanged();
	this->apply();
}

void SyntaxHighlighter::setTheme(const QVariant& theme) {
	this->mTheme = theme;
	emit this->themeChanged();
	this->apply();
}

void SyntaxHighlighter::setRepository(const QVariant& repository) {
	this->mRepository = repository;
	emit this->repositoryChanged();
}

QString SyntaxHighlighter::languageName() const {
	if (auto* definition = this->mDefinition.value<SyntaxDefinition*>()) return definition->name();
	return this->mDefinition.toString();
}

void SyntaxHighlighter::apply() {
	if (!this->mHighlighter) return;
	auto themeName = this->mTheme.toString().toLower();
	auto darkTheme = !themeName.contains(QStringLiteral("light"));
	this->mHighlighter->configure(this->languageName(), darkTheme);
}

}
