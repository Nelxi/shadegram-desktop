/*
This file is part of Shadegram, a patch set for AyuGram Desktop / Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "shadegram/shadegram_background.h"

#include "shadegram/shadegram_default_shader.h"
#include "base/debug_log.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QCursor>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QPainter>
#include <QtGui/QVector3D>
#include <QtGui/QVector4D>
#include <QtWidgets/QWidget>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

namespace Shadegram {
namespace {

constexpr auto kVertex = R"(#version 330 core
void main() {
	vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr auto kPrelude = R"(#version 330 core
uniform vec3 iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform int iFrame;
uniform vec4 iMouse;
out vec4 shadegram_FragColor;
#line 1
)";

constexpr auto kMain = R"(
void main() {
	vec4 c = vec4(0.0, 0.0, 0.0, 1.0);
	mainImage(c, gl_FragCoord.xy);
	shadegram_FragColor = vec4(clamp(c.rgb, 0.0, 1.0), 1.0);
}
)";

struct Settings {
	bool enabled = true;
	QString shader;
	int fps = 30;
	double scale = 1.;
};

[[nodiscard]] QString Folder() {
	const auto appdata = qEnvironmentVariable("APPDATA");
	return (appdata.isEmpty() ? QDir::homePath() : appdata)
		+ u"/shadegram"_q;
}

class Renderer final {
public:
	static Renderer &Instance() {
		// never destroyed: GL objects must not outlive the application object
		static const auto result = new Renderer();
		return *result;
	}

	[[nodiscard]] bool enabled() {
		reloadIfChanged();
		return _settings.enabled && !_failed;
	}

	bool paint(QPainter &p, QSize fill, QRect clip);
	void track(not_null<QWidget*> widget);

private:
	Renderer();

	void reloadIfChanged();
	[[nodiscard]] bool ensureContext();
	[[nodiscard]] bool ensureProgram();
	[[nodiscard]] bool render(QSize size);
	void tick();
	void fail(const QString &why);

	Settings _settings;
	QDateTime _settingsStamp;
	QDateTime _shaderStamp;
	crl::time _lastSettingsCheck = 0;

	std::unique_ptr<QOffscreenSurface> _surface;
	std::unique_ptr<QOpenGLContext> _context;
	std::unique_ptr<QOpenGLFramebufferObject> _fbo;
	std::unique_ptr<QOpenGLShaderProgram> _program;
	std::unique_ptr<QOpenGLVertexArrayObject> _vao;
	bool _failed = false;
	bool _programDirty = true;

	QImage _frame;
	QSize _wanted;
	QElapsedTimer _clock;
	qint64 _lastFrameMs = 0;
	int _frameIndex = 0;

	QTimer _timer;
	std::vector<QPointer<QWidget>> _widgets;
	crl::time _lastPaint = 0;

};

Renderer::Renderer() {
	_clock.start();
	_timer.setTimerType(Qt::PreciseTimer);
	QObject::connect(&_timer, &QTimer::timeout, [this] { tick(); });
	reloadIfChanged();
}

void Renderer::fail(const QString &why) {
	LOG(("Shadegram: %1").arg(why));
	_failed = true;
	_timer.stop();
}

void Renderer::reloadIfChanged() {
	const auto now = crl::now();
	if (_lastSettingsCheck && now - _lastSettingsCheck < 1000) {
		return;
	}
	_lastSettingsCheck = now;
	const auto path = Folder() + u"/desktop.json"_q;
	const auto info = QFileInfo(path);
	const auto stamp = info.exists() ? info.lastModified() : QDateTime();
	if (stamp != _settingsStamp) {
		_settingsStamp = stamp;
		auto settings = Settings();
		auto file = QFile(path);
		if (file.open(QIODevice::ReadOnly)) {
			const auto object = QJsonDocument::fromJson(file.readAll()).object();
			settings.enabled = object.value(u"enabled"_q).toBool(true);
			settings.shader = object.value(u"shader"_q).toString();
			settings.fps = std::clamp(object.value(u"fps"_q).toInt(30), 5, 144);
			settings.scale = std::clamp(object.value(u"scale"_q).toDouble(1.), 0.25, 1.);
		}
		if (settings.shader != _settings.shader) {
			_programDirty = true;
		}
		_settings = settings;
		_failed = false; // a new config may fix it
		_programDirty = true;
	}
	if (!_settings.shader.isEmpty()) {
		const auto shaderInfo = QFileInfo(
			Folder() + u"/shaders/"_q + _settings.shader);
		const auto shaderStamp = shaderInfo.exists()
			? shaderInfo.lastModified()
			: QDateTime();
		if (shaderStamp != _shaderStamp) {
			_shaderStamp = shaderStamp;
			_programDirty = true;
			_failed = false;
		}
	}
}

bool Renderer::ensureContext() {
	if (_context) {
		return true;
	}
	auto format = QSurfaceFormat();
	format.setVersion(3, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setRenderableType(QSurfaceFormat::OpenGL);
	auto context = std::make_unique<QOpenGLContext>();
	context->setFormat(format);
	if (!context->create()) {
		fail(u"could not create an OpenGL 3.3 context"_q);
		return false;
	}
	const auto got = context->format();
	if (got.majorVersion() * 10 + got.minorVersion() < 33) {
		fail(u"OpenGL %1.%2 is too old (3.3 needed)"_q
			.arg(got.majorVersion())
			.arg(got.minorVersion()));
		return false;
	}
	auto surface = std::make_unique<QOffscreenSurface>();
	surface->setFormat(got);
	surface->create();
	if (!surface->isValid() || !context->makeCurrent(surface.get())) {
		fail(u"could not make the OpenGL context current"_q);
		return false;
	}
	_vao = std::make_unique<QOpenGLVertexArrayObject>();
	_vao->create();
	context->doneCurrent();
	_context = std::move(context);
	_surface = std::move(surface);
	LOG(("Shadegram: OpenGL %1.%2 ready.")
		.arg(got.majorVersion())
		.arg(got.minorVersion()));
	return true;
}

bool Renderer::ensureProgram() {
	if (!_programDirty && _program) {
		return true;
	}
	_programDirty = false;
	auto user = QString();
	if (!_settings.shader.isEmpty()) {
		auto file = QFile(Folder() + u"/shaders/"_q + _settings.shader);
		if (file.open(QIODevice::ReadOnly)) {
			user = QString::fromUtf8(file.readAll());
		} else {
			LOG(("Shadegram: shader %1 not found, using the built-in one."
				).arg(_settings.shader));
		}
	}
	if (user.isEmpty()) {
		user = QString::fromUtf8(kDefaultShader);
	}
	auto program = std::make_unique<QOpenGLShaderProgram>();
	const auto fragment = QString::fromUtf8(kPrelude) + user + kMain;
	if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertex)
		|| !program->addShaderFromSourceCode(
			QOpenGLShader::Fragment,
			fragment)
		|| !program->link()) {
		LOG(("Shadegram: shader error: %1").arg(program->log()));
		if (_program) {
			return true; // keep the previous working shader
		}
		fail(u"the shader does not compile"_q);
		return false;
	}
	_program = std::move(program);
	return true;
}

bool Renderer::render(QSize size) {
	if (size.isEmpty() || !ensureContext()) {
		return false;
	}
	if (!_context->makeCurrent(_surface.get())) {
		fail(u"makeCurrent failed"_q);
		return false;
	}
	const auto guard = gsl::finally([&] { _context->doneCurrent(); });
	if (!ensureProgram()) {
		return false;
	}
	if (!_fbo || _fbo->size() != size) {
		_fbo = std::make_unique<QOpenGLFramebufferObject>(size);
		if (!_fbo->isValid()) {
			_fbo = nullptr;
			fail(u"could not create a framebuffer"_q);
			return false;
		}
	}
	const auto f = _context->functions();
	_fbo->bind();
	f->glViewport(0, 0, size.width(), size.height());
	f->glDisable(GL_BLEND);

	const auto nowMs = _clock.elapsed();
	const auto dt = _lastFrameMs ? (nowMs - _lastFrameMs) / 1000.f : 0.f;
	_lastFrameMs = nowMs;

	_program->bind();
	_program->setUniformValue(
		"iResolution",
		QVector3D(size.width(), size.height(), 1.f));
	_program->setUniformValue("iTime", nowMs / 1000.f);
	_program->setUniformValue("iTimeDelta", dt);
	_program->setUniformValue("iFrame", _frameIndex++);

	// Shadertoy mouse: pixels from the bottom-left of the area, z > 0 while
	// the cursor is over it.
	auto mouse = QVector4D(0, 0, 0, 0);
	for (const auto &weak : _widgets) {
		if (const auto widget = weak.data(); widget && widget->isVisible()) {
			const auto local = widget->mapFromGlobal(QCursor::pos());
			if (widget->rect().contains(local)) {
				const auto sx = size.width() / float(widget->width());
				const auto sy = size.height() / float(widget->height());
				mouse = QVector4D(
					local.x() * sx,
					(widget->height() - local.y()) * sy,
					1.f,
					0.f);
			}
			break;
		}
	}
	_program->setUniformValue("iMouse", mouse);

	_vao->bind();
	f->glDrawArrays(GL_TRIANGLES, 0, 3);
	_vao->release();
	_program->release();
	_frame = _fbo->toImage(true);
	_fbo->release();
	return !_frame.isNull();
}

void Renderer::tick() {
	reloadIfChanged();
	auto any = false;
	for (auto i = _widgets.begin(); i != _widgets.end();) {
		if (!*i) {
			i = _widgets.erase(i);
			continue;
		}
		any = any || (*i)->isVisible();
		++i;
	}
	// nobody painted the background for a while (other section, minimized)
	if (!any || crl::now() - _lastPaint > 2000 || !enabled()) {
		_timer.stop();
		return;
	}
	if (!render(_wanted)) {
		return;
	}
	for (const auto &widget : _widgets) {
		if (widget && widget->isVisible()) {
			widget->update();
		}
	}
}

bool Renderer::paint(QPainter &p, QSize fill, QRect clip) {
	if (!enabled() || fill.isEmpty()) {
		return false;
	}
	const auto ratio = p.device() ? p.device()->devicePixelRatioF() : 1.;
	const auto wanted = QSize(
		std::max(1, int(fill.width() * ratio * _settings.scale)),
		std::max(1, int(fill.height() * ratio * _settings.scale)));
	_lastPaint = crl::now();
	// One frame serves every caller (the chat and small previews in boxes):
	// it is rendered at the largest size asked for and scaled down.
	if (_frame.isNull()
		|| wanted.width() > _wanted.width()
		|| wanted.height() > _wanted.height()) {
		_wanted = _wanted.expandedTo(wanted);
		if (!render(_wanted)) {
			return false;
		}
	}
	const auto interval = 1000 / std::max(_settings.fps, 1);
	if (!_timer.isActive() || _timer.interval() != interval) {
		_timer.start(interval);
	}
	const auto sx = _frame.width() / double(fill.width());
	const auto sy = _frame.height() / double(fill.height());
	const auto target = clip.intersected(QRect(QPoint(), fill));
	const auto source = QRectF(
		target.x() * sx,
		target.y() * sy,
		target.width() * sx,
		target.height() * sy);
	p.drawImage(QRectF(target), _frame, source);
	return true;
}

void Renderer::track(not_null<QWidget*> widget) {
	for (const auto &existing : _widgets) {
		if (existing.data() == widget.get()) {
			return;
		}
	}
	_widgets.emplace_back(widget.get());
}

} // namespace

bool Enabled() {
	return Renderer::Instance().enabled();
}

bool PaintBackground(QPainter &p, QSize fill, QRect clip) {
	return Renderer::Instance().paint(p, fill, clip);
}

void Track(not_null<QWidget*> widget) {
	Renderer::Instance().track(widget);
}

} // namespace Shadegram
