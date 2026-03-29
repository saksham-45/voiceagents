#ifndef LMMS_AGENT_CONTROL_VIEW_H
#define LMMS_AGENT_CONTROL_VIEW_H

#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QSet>
#include <QTimer>

#include "ToolPluginView.h"

namespace lmms
{
class AgentControlPlugin;

namespace gui
{

class AgentControlView : public ToolPluginView
{
	Q_OBJECT
public:
	explicit AgentControlView( AgentControlPlugin *plugin );
	~AgentControlView() override;

private slots:
	void runCommand();
	void appendLog( const QString &text );
	void startVoiceCapture();
	void stopVoiceCapture();
	void processVoiceChunks();
	void voiceProcessError( QProcess::ProcessError error );

private:
	void setVoiceUiState( bool recording );
	QString ffmpegPath() const;
	QString whisperPath() const;
	QString whisperModelPath() const;
	QString whisperServiceBaseUrl() const;
	QString whisperServiceModel() const;
	QString voiceChunkDirPath() const;
	bool useWhisperService() const;
	bool ensureWhisperServiceReady( QString& error );
	bool checkWhisperServiceHealth( int timeoutMs, QString& error ) const;
	bool transcribeViaWhisperService( const QString &audioPath, QString &transcript, QString& error ) const;
	bool transcribeAudio( const QString &audioPath, QString &transcript, bool quietNoTranscript );
	void shutdownWhisperService();
	QString microphoneDevice() const;
	QStringList ffmpegCaptureArgs( const QString &outputPath ) const;
	QStringList ffmpegContinuousCaptureArgs( const QString &outputPattern ) const;
	bool looksLikeCommandTranscript( const QString& transcript ) const;
	bool canonicalizeFastCommand( const QString& transcript, QString& command ) const;
	bool dispatchTranscript( const QString& transcript );
	bool runWhisperAndDispatch( const QString &audioPath );

	AgentControlPlugin *m_plugin;
	QLineEdit *m_input;
	QTextEdit *m_log;
	QLabel *m_status;
	QPushButton *m_voiceStartButton;
	QPushButton *m_voiceStopButton;
	QProcess *m_voiceRecordProcess;
	QProcess *m_whisperServiceProcess;
	QTimer *m_voiceChunkTimer;
	QString m_voiceAudioPath;
	QString m_voiceChunkDir;
	QSet<QString> m_processedVoiceChunks;
	QString m_lastDispatchedTranscript;
	QString m_pendingTranscript;
	qint64 m_lastDispatchedAtMs = 0;
	bool m_whisperServiceReady = false;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_AGENT_CONTROL_VIEW_H
