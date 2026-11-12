#include "shader_repair.hpp"

#include <algorithm>

#include <qdiriterator.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <rhi/qshaderbaker.h>
#include <rhi/qshader.h>

namespace qs::macos {

namespace {

Q_LOGGING_CATEGORY(logShaderRepair, "quickshell.macos.shaders", QtInfoMsg);

bool hasMetalVersion(const QShader& shader) {
	return std::ranges::any_of(shader.availableShaders(), [](const QShaderKey& key) {
		return key.source() == QShader::MslShader;
	});
}

QList<QShaderBaker::GeneratedShader> targetsWithMetal(const QShader& shader) {
	QList<QShaderBaker::GeneratedShader> targets;
	for (const auto& key: shader.availableShaders()) {
		if (key.sourceVariant() != QShader::StandardShader) continue;
		QShaderBaker::GeneratedShader target {key.source(), key.sourceVersion()};
		if (!targets.contains(target)) targets << target;
	}
	targets << QShaderBaker::GeneratedShader {QShader::MslShader, QShaderVersion(12)};
	return targets;
}

void rebuildWithMetal(const QString& compiledPath, const QShader& compiledShader) {
	QString sourcePath = compiledPath.chopped(4);
	if (!QFileInfo::exists(sourcePath)) {
		qCWarning(logShaderRepair) << compiledPath << "has no Metal version and no source next to it to build one from";
		return;
	}

	QShaderBaker baker;
	baker.setSourceFileName(sourcePath);
	baker.setGeneratedShaders(targetsWithMetal(compiledShader));
	baker.setGeneratedShaderVariants({QShader::StandardShader});
	QShader rebuiltShader = baker.bake();
	if (!rebuiltShader.isValid()) {
		qCWarning(logShaderRepair) << "Could not build a Metal version of" << sourcePath << baker.errorMessage();
		return;
	}

	QFile compiledFile(compiledPath);
	if (!compiledFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
	compiledFile.write(rebuiltShader.serialized());
	qCInfo(logShaderRepair) << "Added a Metal version to" << compiledPath;
}

}

void addMetalVersionsToShaders(const QString& configDirectory) {
	QDirIterator iterator(configDirectory, {"*.qsb"}, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QString compiledPath = iterator.next();
		QFile compiledFile(compiledPath);
		if (!compiledFile.open(QIODevice::ReadOnly)) continue;
		QShader compiledShader = QShader::fromSerialized(compiledFile.readAll());
		compiledFile.close();
		if (compiledShader.isValid() && !hasMetalVersion(compiledShader)) rebuildWithMetal(compiledPath, compiledShader);
	}
}

}
