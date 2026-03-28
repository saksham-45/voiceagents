#ifndef LMMS_AGENT_CONTROL_H
#define LMMS_AGENT_CONTROL_H

#include <QTcpServer>
#include <QTcpSocket>
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

signals:
	void logMessage(const QString& msg);
	void commandResult(const QString& msg);

private slots:
	void onNewConnection();
	void onSocketReady();
	void onSocketClosed();

private:
	AgentControlService();

	QString dispatchTokens(const QStringList& tokens, const QString& rawText);

	bool importFromDownloads(const QString& fileName, QString& error);
	bool importAudioFile(const QString& path, QString& error);
	bool importProjectFile(const QString& path, QString& error);
	bool addKickPattern(QString& error);

	bool createTrack(Track::Type type, QString& result, QString& error);
	bool createInstrumentTrack(const QString& pluginName, QString& result, QString& error);
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
	SampleTrack* createSampleTrack(const QString& name) const;
	EffectChain* effectChainForTrack(Track* track) const;

	bool addSampleClip(SampleTrack* track, const QString& samplePath, int tickPos);
	QString resolveDownloadsFile(const QString& fileName) const;
	QString defaultKickSample() const;
	QString canonicalPath(const QString& path) const;
	QString joinTokens(const QStringList& tokens, int startIndex) const;
	QString normalizeName(const QString& text) const;

	QTcpServer m_server;
	QSet<QTcpSocket*> m_clients;
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
