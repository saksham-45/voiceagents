#ifndef LMMS_AGENT_CONTROL_H
#define LMMS_AGENT_CONTROL_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QStringList>

#include "Track.h"
#include "ToolPlugin.h"

class QObject;
class QWidget;

namespace lmms
{

class EffectChain;
class InstrumentTrack;
class SampleTrack;
namespace gui
{
class AgentControlView;
}

class AgentControlService : public QObject
{
	Q_OBJECT
public:
	static AgentControlService* instance();
	~AgentControlService() override;

	QString handleCommand(const QString& text);
	QString handleJson(const QJsonObject& obj);
	QJsonObject handleRequest(const QJsonObject& obj);

signals:
	void logMessage(const QString& msg);
	void commandResult(const QString& msg);

private slots:
	void onNewConnection();
	void onSocketReady();
	void onSocketClosed();

private:
	struct Snapshot
	{
		QString id;
		QString label;
		QJsonObject state;
		int actionCounter = 0;
	};

	AgentControlService();

	QString dispatchCommandText(const QString& rawText);
	QStringList tokenizeCommand(const QString& rawText) const;
	bool isUnknownResponse(const QString& response) const;
	bool isFamiliarIntentText(const QString& rawText) const;
	bool applyHeuristicMappings(const QString& rawText, QString& mappedCommand, QString& reason) const;
	bool resolveWithOllama(const QString& rawText, QString& mappedCommand, double& confidence, QString& error) const;
	bool maybeRunTextAgentFallback(const QString& rawText, QString& result, QString& error);

	QString dispatchTokens(const QStringList& tokens, const QString& rawText);
	QJsonObject dispatchTool(const QString& toolName, const QJsonObject& args);
	QJsonObject projectStateObject() const;
	QJsonObject trackObject(const Track* track, int index) const;
	QJsonArray trackArray() const;
	QJsonArray listPatternArray() const;
	QJsonObject diffState(const QJsonObject& before, const QJsonObject& after) const;
	QJsonObject successResponse(const QJsonObject& result = QJsonObject(),
		const QJsonObject& stateDelta = QJsonObject(),
		const QJsonArray& warnings = QJsonArray()) const;
	QJsonObject errorResponse(const QString& errorCode, const QString& errorMessage,
		const QJsonArray& warnings = QJsonArray()) const;
	QString trackTypeName(Track::Type type) const;
	Track* resolveTrackRef(const QJsonObject& args) const;
	InstrumentTrack* resolveInstrumentTrack(const QJsonObject& args) const;
	SampleTrack* resolveSampleTrack(const QJsonObject& args) const;
	QString resolveInstrumentPlugin(const QString& pluginName, QString& displayName) const;
	QString resolveEffectPlugin(const QString& effectName, QString& displayName) const;
	QString toCommandResponseText(const QJsonObject& response) const;
	QJsonArray effectArrayForTrack(Track* track) const;
	QJsonArray availableWindows() const;
	QJsonArray availableTools() const;
	QJsonArray availableInstruments() const;
	QJsonArray availableEffects() const;
	QJsonArray searchProjectAudio(const QString& query) const;
	bool createSnapshot(const QString& label, QJsonObject& result, QString& error);
	bool rollbackSnapshot(const QString& snapshotId, QJsonObject& result, QString& error);
	bool undoLastAction(QJsonObject& result, QString& error);
	bool diffSinceSnapshot(const QString& snapshotId, QJsonObject& result, QString& error) const;
	bool loadSampleToTrack(const QString& samplePath, const QString& trackName, QJsonObject& result, QString& error);
	bool setTempoValue(int tempo, QJsonObject& result, QString& error);
	bool renameTrack(const QString& trackName, const QString& newName, QJsonObject& result, QString& error);
	bool selectTrack(const QString& trackName, QJsonObject& result, QString& error);
	bool setTrackMute(const QString& trackName, bool mute, QJsonObject& result, QString& error);
	bool setTrackSolo(const QString& trackName, bool solo, QJsonObject& result, QString& error);
	bool createPatternClip(const QJsonObject& args, QJsonObject& result, QString& error);
	bool addNotesToPattern(const QJsonObject& args, QJsonObject& result, QString& error);
	bool addStepsToPattern(const QJsonObject& args, QJsonObject& result, QString& error);
	int trackIndex(const Track* track) const;

	bool importFromDownloads(const QString& fileName, QString& error);
	bool importAudioFile(const QString& path, QString& error);
	bool importProjectFile(const QString& path, QString& error);
	bool addKickPattern(QString& error);
	bool addSnarePattern(QString& error);

	bool createTrack(Track::Type type, QString& result, QString& error);
	bool createInstrumentTrack(const QString& pluginName, QString& result, QString& error);
	bool handleSlicerWorkflow(const QString& rawText, const QStringList& tokens, QString& result, QString& error);
	bool ensureSlicerTrack(InstrumentTrack*& track, bool createIfMissing, QString& error);
	bool focusInstrumentTrackWindow(InstrumentTrack* track, QString& error);
	bool loadFileIntoSlicer(const QString& fileQuery, QString& result, QString& error);
	bool sliceSlicerEqual(int segments, QString& result, QString& error);
	bool sliceSlicerByTransients(QString& result, QString& error);
	bool showWindowCommand(const QString& windowName, QString& result, QString& error);
	bool showToolCommand(const QString& toolName, QString& result, QString& error);
	bool newProject(QString& result, QString& error);
	bool openProject(const QString& path, QString& result, QString& error);
	bool saveProject(QString& result, QString& error);
	bool saveProjectAs(const QString& path, QString& result, QString& error);
	bool addEffectToTrack(const QString& effectName, const QString& trackName, QString& result, QString& error);
	bool removeEffectFromTrack(const QString& effectName, const QString& trackName, QString& result, QString& error);

	Track* findTrackByName(const QString& trackName) const;
	Track* findLastTrackOfTypes(const QList<Track::Type>& types) const;
	InstrumentTrack* findInstrumentTrack(const QString& trackName) const;
	InstrumentTrack* findLastSlicerTrack() const;
	SampleTrack* createSampleTrack(const QString& name) const;
	EffectChain* effectChainForTrack(Track* track) const;

	bool addSampleClip(SampleTrack* track, const QString& samplePath, int tickPos);
	QString extractAudioQuery(const QString& rawText, const QStringList& tokens) const;
	QString resolveDownloadsFile(const QString& fileName) const;
	QString resolveDownloadsAudioQuery(const QString& query) const;
	QString defaultKickSample() const;
	QString defaultSnareSample() const;
	QString canonicalPath(const QString& path) const;
	QString joinTokens(const QStringList& tokens, int startIndex) const;
	QString normalizeName(const QString& text) const;

	QTcpServer m_server;
	QSet<QTcpSocket*> m_clients;
	QHash<QTcpSocket*, QByteArray> m_readBuffers;
	QHash<QString, Snapshot> m_snapshots;
	int m_snapshotCounter = 0;
	int m_actionCounter = 0;
	bool m_projectTransitionQueued = false;
	QString m_selectedTrackName;
	QString m_lastImportedAudioPath;
	QString m_lastLoadedInstrument;
};

class AgentControlPlugin : public ToolPlugin
{
	Q_OBJECT
public:
	AgentControlPlugin();
	~AgentControlPlugin() override;

	static AgentControlService* service();

	QString nodeName() const override;
	void saveSettings(QDomDocument&, QDomElement&) override;
	void loadSettings(const QDomElement&) override;
	gui::PluginView* instantiateView(QWidget*) override;

	QString handleCommand(const QString& text);
	QString handleJson(const QJsonObject& obj);

signals:
	void logMessage(const QString& msg);
	void commandResult(const QString& msg);
};

} // namespace lmms

#endif // LMMS_AGENT_CONTROL_H
