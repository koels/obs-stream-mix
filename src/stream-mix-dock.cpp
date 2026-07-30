/*
 * Stream Mix - settings panel (Qt dock).
 *
 * One row per OBS recording track (Track 1..6): include in the Stream Mix,
 * stream-only volume, mute, and limiter. All changes affect streaming only.
 */
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QString>

#include "stream-mix-app.hpp"
#include "stream-mix-config.hpp"

class StreamMixDock : public QWidget {
public:
	StreamMixDock()
	{
		auto *root = new QVBoxLayout(this);

		auto *intro = new QLabel(
			QStringLiteral(
				"Combined mix sent to the normal Start "
				"Streaming button.\nRecording tracks are "
				"unaffected."),
			this);
		intro->setWordWrap(true);
		root->addWidget(intro);

		auto *grid = new QGridLayout();
		grid->addWidget(new QLabel(QStringLiteral("Track"), this), 0, 0);
		grid->addWidget(new QLabel(QStringLiteral("Include"), this), 0,
				1);
		grid->addWidget(new QLabel(QStringLiteral("Volume"), this), 0,
				2);
		grid->addWidget(new QLabel(QStringLiteral("dB"), this), 0, 3);
		grid->addWidget(new QLabel(QStringLiteral("Mute"), this), 0, 4);
		grid->addWidget(new QLabel(QStringLiteral("Limiter"), this), 0,
				5);

		StreamMixConfig *cfg = streammix::config();

		for (int i = 0; i < StreamMixConfig::TRACK_COUNT; i++) {
			const sm_track_cfg &tc = cfg->tracks[i];
			int r = i + 1;

			grid->addWidget(
				new QLabel(QStringLiteral("Track %1").arg(i + 1),
					   this),
				r, 0);

			auto *inc = new QCheckBox(this);
			inc->setChecked(tc.include);
			QObject::connect(inc, &QCheckBox::toggled,
					 [i](bool on) {
						 streammix::set_track_include(
							 i, on);
					 });
			grid->addWidget(inc, r, 1);

			auto *vol = new QSlider(Qt::Horizontal, this);
			vol->setMinimum(-60);
			vol->setMaximum(20);
			vol->setValue((int)tc.gain_db);
			vol->setFixedWidth(140);
			auto *dbLabel = new QLabel(
				QString::number((int)tc.gain_db), this);
			QObject::connect(vol, &QSlider::valueChanged,
					 [i, dbLabel](int v) {
						 dbLabel->setText(
							 QString::number(v));
						 streammix::set_track_gain_db(
							 i, (float)v);
					 });
			grid->addWidget(vol, r, 2);
			grid->addWidget(dbLabel, r, 3);

			auto *mute = new QCheckBox(this);
			mute->setChecked(tc.mute);
			QObject::connect(mute, &QCheckBox::toggled,
					 [i](bool on) {
						 streammix::set_track_mute(i,
									   on);
					 });
			grid->addWidget(mute, r, 4);

			auto *lim = new QCheckBox(this);
			lim->setChecked(tc.limiter);
			QObject::connect(lim, &QCheckBox::toggled,
					 [i, tc](bool on) {
						 streammix::set_track_limiter(
							 i, on, tc.limiter_db);
					 });
			grid->addWidget(lim, r, 5);
		}

		root->addLayout(grid);
		root->addStretch(1);
	}
};

void stream_mix_register_dock()
{
	auto *dock = new StreamMixDock();
	dock->setObjectName("StreamMixDock");
	obs_frontend_add_dock_by_id("stream-mix-dock",
				    obs_module_text("Dock.Title"), dock);
}
