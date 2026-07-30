/*
 * Stream Mix - optional Qt dock for live per-track control.
 *
 * Built only when -DENABLE_QT_UI=ON. Everything here is a thin front-end over
 * the streammix:: bridge; no audio logic lives in the UI.
 */
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QString>

#include <vector>
#include <string>

#include "stream-mix-app.hpp"
#include "stream-mix-config.hpp"

/* Collect names of audio sources routed to a recording track. */
static bool collect_source(void *param, obs_source_t *src)
{
	auto *names = static_cast<std::vector<std::string> *>(param);
	if ((obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) == 0)
		return true;
	if (obs_source_get_audio_mixers(src) == 0)
		return true;
	const char *n = obs_source_get_name(src);
	if (n)
		names->emplace_back(n);
	return true;
}

class StreamMixDock : public QWidget {
public:
	StreamMixDock()
	{
		auto *root = new QVBoxLayout(this);

		toggleBtn = new QPushButton(this);
		refreshToggle();
		connect(toggleBtn, &QPushButton::clicked, this, [this]() {
			if (streammix::active())
				streammix::stop();
			else
				streammix::start();
			refreshToggle();
		});
		root->addWidget(toggleBtn);

		auto *refreshBtn = new QPushButton(
			QStringLiteral("Refresh sources"), this);
		connect(refreshBtn, &QPushButton::clicked, this,
			[this]() { rebuild(); });
		root->addWidget(refreshBtn);

		auto *scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		rows = new QWidget(scroll);
		rowsLayout = new QVBoxLayout(rows);
		rowsLayout->setAlignment(Qt::AlignTop);
		scroll->setWidget(rows);
		root->addWidget(scroll, 1);

		rebuild();
	}

private:
	void refreshToggle()
	{
		toggleBtn->setText(streammix::active()
					   ? QStringLiteral("Stop Stream Mix")
					   : QStringLiteral("Start Stream Mix"));
	}

	void rebuild()
	{
		/* Clear existing rows. */
		QLayoutItem *item;
		while ((item = rowsLayout->takeAt(0)) != nullptr) {
			if (item->widget())
				item->widget()->deleteLater();
			delete item;
		}

		std::vector<std::string> names;
		obs_enum_sources(collect_source, &names);

		StreamMixConfig *cfg = streammix::config();

		for (const std::string &name : names) {
			sm_track_cfg tc;
			auto it = cfg->tracks.find(name);
			if (it != cfg->tracks.end())
				tc = it->second;

			auto *row = new QWidget(rows);
			auto *hl = new QHBoxLayout(row);

			hl->addWidget(new QLabel(QString::fromStdString(name),
						 row),
				      1);

			auto *vol = new QSlider(Qt::Horizontal, row);
			vol->setMinimum(-60);
			vol->setMaximum(20);
			vol->setValue((int)tc.gain_db);
			vol->setFixedWidth(120);
			const std::string n = name;
			connect(vol, &QSlider::valueChanged, this,
				[n](int v) {
					streammix::set_track_gain_db(n,
								     (float)v);
				});
			hl->addWidget(vol);

			auto *mute = new QCheckBox(
				QStringLiteral("Mute"), row);
			mute->setChecked(tc.mute);
			connect(mute, &QCheckBox::toggled, this,
				[n](bool on) {
					streammix::set_track_mute(n, on);
				});
			hl->addWidget(mute);

			auto *excl = new QCheckBox(
				QStringLiteral("Exclude"), row);
			excl->setChecked(tc.exclude);
			connect(excl, &QCheckBox::toggled, this,
				[n](bool on) {
					streammix::set_track_exclude(n, on);
				});
			hl->addWidget(excl);

			rowsLayout->addWidget(row);
		}
	}

	QPushButton *toggleBtn = nullptr;
	QWidget *rows = nullptr;
	QVBoxLayout *rowsLayout = nullptr;
};

void stream_mix_register_dock()
{
	auto *dock = new StreamMixDock();
	dock->setObjectName("StreamMixDock");
	obs_frontend_add_dock_by_id("stream-mix-dock",
				    obs_module_text("Dock.Title"), dock);
}
