/* qv4l2: a control panel controlling v4l2 devices.
 *
 * Copyright (C) 2006 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include "general-tab.h"
#include "../libv4l2util/libv4l2util.h"

#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QDoubleValidator>

#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <QRegExp>

bool GeneralTab::m_fullAudioName = false;

enum audioDeviceAdd {
	AUDIO_ADD_NO,
	AUDIO_ADD_READ,
	AUDIO_ADD_WRITE,
	AUDIO_ADD_READWRITE
};

static QString pixfmt2s(unsigned id)
{
	QString pixfmt;

	pixfmt += (char)(id & 0xff);
	pixfmt += (char)((id >> 8) & 0xff);
	pixfmt += (char)((id >> 16) & 0xff);
	pixfmt += (char)((id >> 24) & 0xff);
	return pixfmt;
}

GeneralTab::GeneralTab(const QString &device, cv4l_fd *_fd, int n, QWidget *parent) :
	QGridLayout(parent),
	cv4l_fd(_fd),
	m_row(0),
	m_col(0),
	m_cols(n),
	m_minWidth(175),
	m_pxw(25.0),
	m_vMargin(10),
	m_hMargin(20),
	m_maxh(0),
	m_isRadio(false),
	m_isSDR(false),
	m_isVbi(false),
	m_isOutput(false),
	m_freqFac(16),
	m_freqRfFac(16),
	m_isPlanar(false),
	m_haveBuffers(false),
	m_discreteSizes(false),
	m_videoInput(NULL),
	m_videoOutput(NULL),
	m_audioInput(NULL),
	m_audioOutput(NULL),
	m_tvStandard(NULL),
	m_qryStandard(NULL),
	m_videoTimings(NULL),
	m_pixelAspectRatio(NULL),
	m_colorspace(NULL),
	m_displayColorspace(NULL),
	m_cropping(NULL),
	m_qryTimings(NULL),
	m_freq(NULL),
	m_freqTable(NULL),
	m_freqChannel(NULL),
	m_audioMode(NULL),
	m_subchannels(NULL),
	m_freqRf(NULL),
	m_stereoMode(NULL),
	m_rdsMode(NULL),
	m_detectSubchans(NULL),
	m_vidCapFormats(NULL),
	m_vidFields(NULL),
	m_frameSize(NULL),
	m_frameWidth(NULL),
	m_frameHeight(NULL),
	m_frameInterval(NULL),
	m_vidOutFormats(NULL),
	m_capMethods(NULL),
	m_vbiMethods(NULL),
	m_audioInDevice(NULL),
	m_audioOutDevice(NULL),
	m_cropWidth(NULL),
	m_cropLeft(NULL),
	m_cropHeight(NULL),
	m_cropTop(NULL),
	m_composeWidth(NULL),
	m_composeLeft(NULL),
	m_composeHeight(NULL),
	m_composeTop(NULL)
{
	m_device.append(device);
	setSizeConstraint(QLayout::SetMinimumSize);

	for (int i = 0; i < n; i++) {
		m_maxw[i] = 0;
	}

	cv4l_ioctl(VIDIOC_QUERYCAP, &m_querycap);

	addTitle("General Information");

	addLabel("Device");
	addLabel(device + (g_direct() ? "" : " (wrapped)"));

	addLabel("Driver");
	addLabel((char *)m_querycap.driver);

	addLabel("Card");
	addLabel((char *)m_querycap.card);

	addLabel("Bus");
	addLabel((char *)m_querycap.bus_info);

	g_tuner(m_tuner);
	g_tuner(m_tuner_rf, 1);
	g_modulator(m_modulator);

	v4l2_input vin;
	v4l2_output vout;
	v4l2_audio vaudio;
	v4l2_audioout vaudout;
	v4l2_fmtdesc fmt;
	bool needsStd = false;
	bool needsTimings = false;

	if (m_tuner.capability &&
	    (m_tuner.capability & (V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_1HZ)))
		m_isRadio = true;
	if (m_modulator.capability &&
	    (m_modulator.capability & (V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_1HZ)))
		m_isRadio = true;
	if (m_querycap.capabilities & V4L2_CAP_DEVICE_CAPS) {
		m_isVbi = g_caps() & (V4L2_CAP_VBI_CAPTURE | V4L2_CAP_SLICED_VBI_CAPTURE);
		m_isSDR = g_caps() & V4L2_CAP_SDR_CAPTURE;
		if (m_isSDR)
			m_isRadio = true;
		if (g_caps() & (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE))
			m_isOutput = true;
	}

	if (m_querycap.capabilities &
		(V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE))
		m_isPlanar = true;

	m_stackedStandards = new QStackedWidget;
	m_stackedFrameSettings = new QStackedWidget;
	m_stackedFrequency = new QStackedWidget;

	if (!enum_input(vin, true) || m_tuner.capability) {
		addTitle("Input Settings");
		inputSection(needsStd, needsTimings, vin);
	}

	if (m_tuner_rf.capability || m_modulator.capability || (!isRadio() && !enum_output(vout, true))) {
		addTitle("Output Settings");
		outputSection(vout, fmt);
	}

	if (hasAlsaAudio()) {
		m_audioInDevice = new QComboBox(parent);
		m_audioOutDevice = new QComboBox(parent);
	}

	if (!isVbi() && (createAudioDeviceList() || (!isRadio() && !enum_audio(vaudio, true)) ||
	    (!isSDR() && m_tuner.capability) || (!isRadio() && !enum_audout(vaudout, true)))) {
		addTitle("Audio Settings");
		audioSection(vaudio, vaudout); 
	}
	
	if (hasAlsaAudio() && !createAudioDeviceList())
	{
		delete m_audioInDevice;
		delete m_audioOutDevice;
		m_audioInDevice = NULL;
		m_audioOutDevice = NULL;
	}
	
	if (isRadio())
		goto done;

	addTitle("Format Settings");
	if (isVbi()) {
		addLabel("VBI Capture Method");
		m_vbiMethods = new QComboBox(parent);
		if (g_caps() & V4L2_CAP_VBI_CAPTURE)
			m_vbiMethods->addItem("Raw");
		if (g_caps() & V4L2_CAP_SLICED_VBI_CAPTURE)
			m_vbiMethods->addItem("Sliced");
		addWidget(m_vbiMethods);
		connect(m_vbiMethods, SIGNAL(activated(int)), SLOT(vbiMethodsChanged(int)));
		vbiMethodsChanged(0);
		updateVideoInput();
		goto capture_method;
	}
	formatSection(fmt);

capture_method:
	addLabel("Capture Method");
	m_capMethods = new QComboBox(parent);
	if (g_caps() & V4L2_CAP_STREAMING) {
		cv4l_queue q;

		// Yuck. The videobuf framework does not accept a reqbufs count of 0.
		// This is out-of-spec, but it means that the only way to test which
		// method is supported is to give it a non-zero count. But after that
		// we have to reopen the device to clear the fact that there were
		// buffers allocated. This is the only really portable way as long
		// as there are still drivers around that do not support reqbufs(0).
		q.init(g_type(), V4L2_MEMORY_USERPTR);
		if (q.reqbufs(this, 1) == 0) {
			m_capMethods->addItem("User pointer I/O", QVariant(methodUser));
			reopen(true);
		}
		q.init(g_type(), V4L2_MEMORY_MMAP);
		if (q.reqbufs(this, 1) == 0) {
			m_capMethods->addItem("Memory mapped I/O", QVariant(methodMmap));
			reopen(true);
		}
	}
	if (g_caps() & V4L2_CAP_READWRITE) {
		m_capMethods->addItem("read()", QVariant(methodRead));
	}
	addWidget(m_capMethods);



	if (!isRadio() && !isVbi() && !m_isOutput && (has_crop() || has_compose())) {
		addTitle("Cropping & Compose Settings");
		cropSection();
	}

	updateVideoInput();
	updateVideoOutput();
	updateVidFormat();

done:
	QGridLayout::addWidget(new QWidget(parent), rowCount(), 0, 1, n);
	setRowStretch(rowCount() - 1, 1);
	if (m_videoInput)
		updateGUI(m_videoInput->currentIndex());
	else
		updateGUI(0);
	fixWidth();
}

void GeneralTab::inputSection(bool needsStd, bool needsTimings, v4l2_input vin)
{
	if (!isRadio() && !enum_input(vin, true)) {
		addLabel("Input");
		m_videoInput = new QComboBox(parentWidget());
		do {
			m_videoInput->addItem((char *)vin.name);
			if (vin.capabilities & V4L2_IN_CAP_STD)
				needsStd = true;
			if (vin.capabilities & V4L2_IN_CAP_DV_TIMINGS)
				needsTimings = true;

			struct v4l2_event_subscription sub = {
				V4L2_EVENT_SOURCE_CHANGE, vin.index
			};

			subscribe_event(sub);
		} while (!enum_input(vin));
		addWidget(m_videoInput);
		connect(m_videoInput, SIGNAL(activated(int)), SLOT(inputChanged(int)));
		m_row++;
		m_col = 0;
	}

	QWidget *wStd = new QWidget();
	QGridLayout *m_stdRow = new QGridLayout(wStd);
	m_grids.append(m_stdRow);

	if (needsStd) {
		v4l2_std_id tmp;

		m_tvStandard = new QComboBox(parentWidget());
		m_stdRow->addWidget(new QLabel("TV Standard", parentWidget()), 0, 0, Qt::AlignLeft);
		m_stdRow->addWidget(m_tvStandard, 0, 1, Qt::AlignLeft);
		connect(m_tvStandard, SIGNAL(activated(int)), SLOT(standardChanged(int)));
		refreshStandards();
		if (ioctl_exists(cv4l_ioctl(VIDIOC_QUERYSTD, &tmp))) {
			m_qryStandard = new QToolButton(parentWidget());
			m_qryStandard->setIcon(QIcon(":/enterbutt.png"));
			m_stdRow->addWidget(new QLabel("Query Standard", parentWidget()), 0, 2, Qt::AlignLeft);
			m_stdRow->addWidget(m_qryStandard, 0, 3, Qt::AlignLeft);
			connect(m_qryStandard, SIGNAL(clicked()), SLOT(qryStdClicked()));
		}
	}

	QWidget *wTim = new QWidget();
	QGridLayout *m_timRow = new QGridLayout(wTim);
	m_grids.append(m_timRow);

	if (needsTimings) {
		m_videoTimings = new QComboBox(parentWidget());
		m_timRow->addWidget(new QLabel("Video Timings", parentWidget()), 0, 0, Qt::AlignLeft);
		m_timRow->addWidget(m_videoTimings, 0, 1, Qt::AlignLeft);
		connect(m_videoTimings, SIGNAL(activated(int)), SLOT(timingsChanged(int)));
		refreshTimings();
		m_qryTimings = new QToolButton(parentWidget());
		m_qryTimings->setIcon(QIcon(":/enterbutt.png"));
		m_timRow->addWidget(new QLabel("Query Timings", parentWidget()), 0, 2, Qt::AlignLeft);
		m_timRow->addWidget(m_qryTimings, 0, 3, Qt::AlignLeft);
		connect(m_qryTimings, SIGNAL(clicked()), SLOT(qryTimingsClicked()));
	}

	m_stackedStandards->addWidget(wStd);
	m_stackedStandards->addWidget(wTim);
	QGridLayout::addWidget(m_stackedStandards, m_row, 0, 1, m_cols, Qt::AlignVCenter);
	m_row++;

	QWidget *wFreq = new QWidget();
	QGridLayout *m_freqRows = new QGridLayout(wFreq);
	m_grids.append(m_freqRows);

	if (m_tuner.capability) {
		const char *unit = (m_tuner.capability & V4L2_TUNER_CAP_LOW) ? " kHz" :
			(m_tuner.capability & V4L2_TUNER_CAP_1HZ ? " Hz" : " MHz");

		m_freqFac = (m_tuner.capability & V4L2_TUNER_CAP_1HZ) ? 1 : 16;
		m_freq = new QDoubleSpinBox(parentWidget());
		m_freq->setMinimum(m_tuner.rangelow / m_freqFac);
		m_freq->setMaximum(m_tuner.rangehigh / m_freqFac);
		m_freq->setSingleStep(1.0 / m_freqFac);
		m_freq->setSuffix(unit);
		m_freq->setDecimals((m_tuner.capability & V4L2_TUNER_CAP_1HZ) ? 0 : 4);
		m_freq->setWhatsThis(QString("Frequency\nLow: %1 %3\nHigh: %2 %3")
				     .arg((double)m_tuner.rangelow / m_freqFac, 0, 'f', 2)
				     .arg((double)m_tuner.rangehigh / m_freqFac, 0, 'f', 2)
				     .arg(unit));
		m_freq->setStatusTip(m_freq->whatsThis());
		connect(m_freq, SIGNAL(valueChanged(double)), SLOT(freqChanged(double)));
		updateFreq();
		m_freqRows->addWidget(new QLabel("Frequency", parentWidget()), 0, 0, Qt::AlignLeft);
		m_freqRows->addWidget(m_freq, 0, 1, Qt::AlignLeft);
	}

	if (m_tuner.capability && !isSDR()) {
		QLabel *l = new QLabel("Refresh Tuner Status", parentWidget());
		QWidget *w = new QWidget(parentWidget());
		QHBoxLayout *box = new QHBoxLayout(w);

		box->setMargin(0);
		m_detectSubchans = new QToolButton(w);
		m_detectSubchans->setIcon(QIcon(":/enterbutt.png"));
		m_subchannels = new QLabel("", w);
		box->addWidget(m_detectSubchans, 0, Qt::AlignLeft);
		box->addWidget(m_subchannels, 0, Qt::AlignLeft);
		m_freqRows->addWidget(l, 0, 2, Qt::AlignLeft);
		m_freqRows->addWidget(w, 0, 3, Qt::AlignLeft);
		connect(m_detectSubchans, SIGNAL(clicked()), SLOT(detectSubchansClicked()));
		detectSubchansClicked();
	}

	if (m_tuner.capability && !isRadio()) {
		m_freqTable = new QComboBox(parentWidget());
		for (int i = 0; v4l2_channel_lists[i].name; i++) {
			m_freqTable->addItem(v4l2_channel_lists[i].name);
		}
		m_freqRows->addWidget(new QLabel("Frequency Table", parentWidget()), 1, 0, Qt::AlignLeft);
		m_freqRows->addWidget(m_freqTable, 1, 1, Qt::AlignLeft);
		connect(m_freqTable, SIGNAL(activated(int)), SLOT(freqTableChanged(int)));

		m_freqChannel = new QComboBox(parentWidget());
		m_freqRows->addWidget(new QLabel("Channels", parentWidget()), 1, 2, Qt::AlignLeft);
		m_freqRows->addWidget(m_freqChannel, 1, 3, Qt::AlignLeft);
		connect(m_freqChannel, SIGNAL(activated(int)), SLOT(freqChannelChanged(int)));
		updateFreqChannel();
	}

	m_stackedFrequency->addWidget(wFreq);
	QGridLayout::addWidget(m_stackedFrequency, m_row, 0, 2, m_cols, Qt::AlignVCenter);
	m_row += 2;

	QWidget *wFrameWH = new QWidget();
	QWidget *wFrameSR = new QWidget();
	QGridLayout *m_wh = new QGridLayout(wFrameWH);
	QGridLayout *m_sr = new QGridLayout(wFrameSR);
	m_grids.append(m_wh);
	m_grids.append(m_sr);

	m_wh->addWidget(new QLabel("Frame Width", parentWidget()), 0, 0, Qt::AlignLeft);
	m_frameWidth = new QSpinBox(parentWidget());
	m_wh->addWidget(m_frameWidth, 0, 1, Qt::AlignLeft);
	connect(m_frameWidth, SIGNAL(editingFinished()), SLOT(frameWidthChanged()));

	m_wh->addWidget(new QLabel("Frame Height", parentWidget()), 0, 2, Qt::AlignLeft);
	m_frameHeight = new QSpinBox(parentWidget());
	m_wh->addWidget(m_frameHeight, 0, 3, Qt::AlignLeft);
	connect(m_frameHeight, SIGNAL(editingFinished()), SLOT(frameHeightChanged()));

	m_sr->addWidget(new QLabel("Frame Size", parentWidget()), 0, 0, Qt::AlignLeft);
	m_frameSize = new QComboBox(parentWidget());
	m_sr->addWidget(m_frameSize, 0, 1, Qt::AlignLeft);
	connect(m_frameSize, SIGNAL(activated(int)), SLOT(frameSizeChanged(int)));

	m_sr->addWidget(new QLabel("Frame Rate", parentWidget()), 0, 2, Qt::AlignLeft);
	m_frameInterval = new QComboBox(parentWidget());
	m_sr->addWidget(m_frameInterval, 0, 3, Qt::AlignLeft);
	connect(m_frameInterval, SIGNAL(activated(int)), SLOT(frameIntervalChanged(int)));

	m_stackedFrameSettings->addWidget(wFrameWH);
	m_stackedFrameSettings->addWidget(wFrameSR);

	QGridLayout::addWidget(m_stackedFrameSettings, m_row, 0, 1, m_cols, Qt::AlignVCenter);
	m_row++;
}

void GeneralTab::outputSection(v4l2_output vout, v4l2_fmtdesc fmt)
{
	if (!isRadio() && !enum_output(vout, true)) {
		addLabel("Output");
		m_videoOutput = new QComboBox(parentWidget());
		do {
			m_videoOutput->addItem((char *)vout.name);
		} while (!enum_output(vout));
		addWidget(m_videoOutput);
		connect(m_videoOutput, SIGNAL(activated(int)), SLOT(outputChanged(int)));
		updateVideoOutput();
	}

	if (m_isOutput) {
		addLabel("Output Image Formats");
		m_vidOutFormats = new QComboBox(parentWidget());
		m_vidOutFormats->setMinimumContentsLength(20);
		if (!enum_fmt(fmt, true)) {
			do {
				m_vidOutFormats->addItem(pixfmt2s(fmt.pixelformat) +
					" - " + (const char *)fmt.description);
			} while (!enum_fmt(fmt));
		}
		addWidget(m_vidOutFormats);
		connect(m_vidOutFormats, SIGNAL(activated(int)), SLOT(vidOutFormatChanged(int)));
	}


	if (m_tuner_rf.capability) {
		const char *unit = (m_tuner_rf.capability & V4L2_TUNER_CAP_LOW) ? " kHz" :
			(m_tuner_rf.capability & V4L2_TUNER_CAP_1HZ ? " Hz" : " MHz");

		m_freqRfFac = (m_tuner_rf.capability & V4L2_TUNER_CAP_1HZ) ? 1 : 16;
		m_freqRf = new QDoubleSpinBox(parentWidget());
		m_freqRf->setMinimum(m_tuner_rf.rangelow / m_freqRfFac);
		m_freqRf->setMaximum(m_tuner_rf.rangehigh / m_freqRfFac);
		m_freqRf->setSingleStep(1.0 / m_freqRfFac);
		m_freqRf->setSuffix(unit);
		m_freqRf->setDecimals((m_tuner_rf.capability & V4L2_TUNER_CAP_1HZ) ? 0 : 4);
		m_freqRf->setWhatsThis(QString("RF Frequency\nLow: %1 %3\nHigh: %2 %3")
				    .arg((double)m_tuner_rf.rangelow / m_freqRfFac, 0, 'f', 2)
				    .arg((double)m_tuner_rf.rangehigh / m_freqRfFac, 0, 'f', 2)
				    .arg(unit));
		m_freqRf->setStatusTip(m_freqRf->whatsThis());
		connect(m_freqRf, SIGNAL(valueChanged(double)), SLOT(freqRfChanged(double)));
		updateFreqRf();
		addLabel("RF Frequency");
		addWidget(m_freqRf);
	}

	if (m_modulator.capability) {
		const char *unit = (m_modulator.capability & V4L2_TUNER_CAP_LOW) ? " kHz" :
			(m_modulator.capability & V4L2_TUNER_CAP_1HZ ? " Hz" : " MHz");

		m_freqFac = (m_modulator.capability & V4L2_TUNER_CAP_1HZ) ? 1 : 16;
		m_freq = new QDoubleSpinBox(parentWidget());
		m_freq->setMinimum(m_modulator.rangelow / m_freqFac);
		m_freq->setMaximum(m_modulator.rangehigh / m_freqFac);
		m_freq->setSingleStep(1.0 / m_freqFac);
		m_freq->setSuffix(unit);
		m_freq->setDecimals((m_modulator.capability & V4L2_TUNER_CAP_1HZ) ? 0 : 4);
		m_freq->setWhatsThis(QString("Frequency\nLow: %1 %3\nHigh: %2 %3")
				    .arg((double)m_modulator.rangelow / m_freqFac, 0, 'f', 2)
				    .arg((double)m_modulator.rangehigh / m_freqFac, 0, 'f', 2)
				    .arg(unit));
		m_freq->setStatusTip(m_freq->whatsThis());
		connect(m_freq, SIGNAL(valueChanged(double)), SLOT(freqChanged(double)));
		updateFreq();
		addLabel("Frequency");
		addWidget(m_freq);
	}
	if (m_modulator.capability && !isSDR()) {
		if (m_modulator.capability & V4L2_TUNER_CAP_STEREO) {
			addLabel("Stereo");
			m_stereoMode = new QCheckBox(parentWidget());
			m_stereoMode->setCheckState((m_modulator.txsubchans & V4L2_TUNER_SUB_STEREO) ?
					Qt::Checked : Qt::Unchecked);
			addWidget(m_stereoMode);
			connect(m_stereoMode, SIGNAL(clicked()), SLOT(stereoModeChanged()));
		}
		if (m_modulator.capability & V4L2_TUNER_CAP_RDS) {
			addLabel("RDS");
			m_rdsMode = new QCheckBox(parentWidget());
			m_rdsMode->setCheckState((m_modulator.txsubchans & V4L2_TUNER_SUB_RDS) ?
					Qt::Checked : Qt::Unchecked);
			addWidget(m_rdsMode);
			connect(m_rdsMode, SIGNAL(clicked()), SLOT(rdsModeChanged()));
		}
	}
}

void GeneralTab::audioSection(v4l2_audio vaudio, v4l2_audioout vaudout)
{
	if (hasAlsaAudio()) {
		if (createAudioDeviceList()) {
			addLabel("Audio Input Device");
			connect(m_audioInDevice, SIGNAL(activated(int)), SLOT(changeAudioDevice()));
			addWidget(m_audioInDevice);

			addLabel("Audio Output Device");
			connect(m_audioOutDevice, SIGNAL(activated(int)), SLOT(changeAudioDevice()));
			addWidget(m_audioOutDevice);

			if (isRadio()) {
				setAudioDeviceBufferSize(75);
			} else {
				v4l2_fract fract;
				if (cv4l_fd::get_interval(fract)) {
					// Default values are for 30 FPS
					fract.numerator = 33;
					fract.denominator = 1000;
				}
				// Standard capacity is two frames
				setAudioDeviceBufferSize((fract.numerator * 2000) / fract.denominator);
			}
		} else {
			delete m_audioInDevice;
			delete m_audioOutDevice;
			m_audioInDevice = NULL;
			m_audioOutDevice = NULL;
		}
	}

	if (!isRadio() && !enum_audio(vaudio, true)) {
		addLabel("Input Audio");
		m_audioInput = new QComboBox(parentWidget());
		m_audioInput->setMinimumContentsLength(10);
		do {
			m_audioInput->addItem((char *)vaudio.name);
		} while (!enum_audio(vaudio));
		addWidget(m_audioInput);
		connect(m_audioInput, SIGNAL(activated(int)), SLOT(inputAudioChanged(int)));
		updateAudioInput();
	}

	if (m_tuner.capability && !isSDR()) {
		addLabel("Audio Mode");
		m_audioMode = new QComboBox(parentWidget());
		m_audioMode->setMinimumContentsLength(12);
		m_audioMode->addItem("Mono");
		int audIdx = 0;
		m_audioModes[audIdx++] = V4L2_TUNER_MODE_MONO;
		if (m_tuner.capability & V4L2_TUNER_CAP_STEREO) {
			m_audioMode->addItem("Stereo");
			m_audioModes[audIdx++] = V4L2_TUNER_MODE_STEREO;
		}
		if (m_tuner.capability & V4L2_TUNER_CAP_LANG1) {
			m_audioMode->addItem("Language 1");
			m_audioModes[audIdx++] = V4L2_TUNER_MODE_LANG1;
		}
		if (m_tuner.capability & V4L2_TUNER_CAP_LANG2) {
			m_audioMode->addItem("Language 2");
			m_audioModes[audIdx++] = V4L2_TUNER_MODE_LANG2;
		}
		if ((m_tuner.capability & (V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2)) ==
				(V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2)) {
			m_audioMode->addItem("Language 1+2");
			m_audioModes[audIdx++] = V4L2_TUNER_MODE_LANG1_LANG2;
		}
		addWidget(m_audioMode);
		for (int i = 0; i < audIdx; i++)
			if (m_audioModes[i] == m_tuner.audmode)
				m_audioMode->setCurrentIndex(i);
		connect(m_audioMode, SIGNAL(activated(int)), SLOT(audioModeChanged(int)));
	}

	if (!isRadio() && !enum_audout(vaudout, true)) {
		addLabel("Output Audio");
		m_audioOutput = new QComboBox(parentWidget());
		m_audioOutput->setMinimumContentsLength(10);
		do {
			m_audioOutput->addItem((char *)vaudout.name);
		} while (!enum_audout(vaudout));
		addWidget(m_audioOutput);
		connect(m_audioOutput, SIGNAL(activated(int)), SLOT(outputAudioChanged(int)));
		updateAudioOutput();
	}
}

void GeneralTab::formatSection(v4l2_fmtdesc fmt)
{
	if (!m_isOutput) {
		addLabel("Capture Image Formats");
		m_vidCapFormats = new QComboBox(parentWidget());
		m_vidCapFormats->setMinimumContentsLength(20);
		if (!enum_fmt(fmt, true)) {
			do {
				QString s(pixfmt2s(fmt.pixelformat) + " (");

				if (fmt.flags & V4L2_FMT_FLAG_EMULATED)
					m_vidCapFormats->addItem(s + "Emulated)");
				else
					m_vidCapFormats->addItem(s + (const char *)fmt.description + ")");
			} while (!enum_fmt(fmt));
		}
		addWidget(m_vidCapFormats);
		connect(m_vidCapFormats, SIGNAL(activated(int)), SLOT(vidCapFormatChanged(int)));
	}

	addLabel("Field");
	m_vidFields = new QComboBox(parentWidget());
	m_vidFields->setMinimumContentsLength(21);
	addWidget(m_vidFields);
	connect(m_vidFields, SIGNAL(activated(int)), SLOT(vidFieldChanged(int)));

	m_cropping = new QComboBox(parentWidget());
	m_cropping->addItem("Source Width and Height");
	m_cropping->addItem("Crop Top and Bottom Line");
	m_cropping->addItem("Traditional 4:3");
	m_cropping->addItem("Widescreen 14:9");
	m_cropping->addItem("Widescreen 16:9");
	m_cropping->addItem("Cinema 1.85:1");
	m_cropping->addItem("Cinema 2.39:1");

	addLabel("Video Aspect Ratio");
	addWidget(m_cropping);
	connect(m_cropping, SIGNAL(activated(int)), SIGNAL(croppingChanged()));

	if (!isRadio() && !isVbi()) {
		m_pixelAspectRatio = new QComboBox(parentWidget());
		m_pixelAspectRatio->addItem("Autodetect");
		m_pixelAspectRatio->addItem("Square");
		m_pixelAspectRatio->addItem("NTSC/PAL-M/PAL-60");
		m_pixelAspectRatio->addItem("NTSC/PAL-M/PAL-60, Anamorphic");
		m_pixelAspectRatio->addItem("PAL/SECAM");
		m_pixelAspectRatio->addItem("PAL/SECAM, Anamorphic");

		// Update hints by calling a get
		getPixelAspectRatio();

		addLabel("Pixel Aspect Ratio");
		addWidget(m_pixelAspectRatio);
		connect(m_pixelAspectRatio, SIGNAL(activated(int)), SLOT(changePixelAspectRatio()));

#ifdef HAVE_QTGL
		m_colorspace = new QComboBox(parentWidget());
		m_colorspace->addItem("Autodetect");
		m_colorspace->addItem("SMPTE 170M");
		m_colorspace->addItem("SMPTE 240M");
		m_colorspace->addItem("REC 709");
		m_colorspace->addItem("470 System M");
		m_colorspace->addItem("470 System BG");
		m_colorspace->addItem("sRGB");

		addLabel("Colorspace");
		addWidget(m_colorspace);
		connect(m_colorspace, SIGNAL(activated(int)), SIGNAL(colorspaceChanged()));

		m_displayColorspace = new QComboBox(parentWidget());
		m_displayColorspace->addItem("sRGB");
		m_displayColorspace->addItem("Linear RGB");
		m_displayColorspace->addItem("REC 709");
		m_displayColorspace->addItem("SMPTE 240M");

		addLabel("Display Colorspace");
		addWidget(m_displayColorspace);
		connect(m_displayColorspace, SIGNAL(activated(int)), SIGNAL(displayColorspaceChanged()));
#endif
	}
}

void GeneralTab::cropSection()
{
	if (has_crop()) {
		m_cropWidth = new QSlider(Qt::Horizontal, parentWidget());
		m_cropWidth->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
		m_cropWidth->setRange(1, 100);
		m_cropWidth->setSliderPosition(100);
		addLabel("Crop Width");
		addWidget(m_cropWidth);
		connect(m_cropWidth, SIGNAL(valueChanged(int)), SLOT(cropChanged()));

		m_cropLeft = new QSlider(Qt::Horizontal, parentWidget());
		m_cropLeft->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
		m_cropLeft->setRange(0, 100);
		m_cropLeft->setSliderPosition(0);
		addLabel("Crop Left Offset");
		addWidget(m_cropLeft);
		connect(m_cropLeft, SIGNAL(valueChanged(int)), SLOT(cropChanged()));

		m_cropHeight = new QSlider(Qt::Horizontal, parentWidget());
		m_cropHeight->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
		m_cropHeight->setRange(1, 100);
		m_cropHeight->setSliderPosition(100);
		addLabel("Crop Height");
		addWidget(m_cropHeight);
		connect(m_cropHeight, SIGNAL(valueChanged(int)), SLOT(cropChanged()));

		m_cropTop = new QSlider(Qt::Horizontal, parentWidget());
		m_cropTop->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
		m_cropTop->setRange(0, 100);
		m_cropTop->setSliderPosition(0);
		addLabel("Crop Top Offset");
		addWidget(m_cropTop);
		connect(m_cropTop, SIGNAL(valueChanged(int)), SLOT(cropChanged()));
	}

	if (has_compose()) {
		m_composeWidth = new QSlider(Qt::Horizontal, parentWidget());
		m_composeWidth->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
		m_composeWidth->setRange(1, 100);
		m_composeWidth->setSliderPosition(100);
		addLabel("Compose Width");
		addWidget(m_composeWidth);
		connect(m_composeWidth, SIGNAL(valueChanged(int)), SLOT(composeChanged()));

		m_composeLeft = new QSlider(Qt::Horizontal, parentWidget());
		m_composeLeft->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
		m_composeLeft->setRange(0, 100);
		m_composeLeft->setSliderPosition(0);
		addLabel("Compose Left Offset");
		addWidget(m_composeLeft);
		connect(m_composeLeft, SIGNAL(valueChanged(int)), SLOT(composeChanged()));

		m_composeHeight = new QSlider(Qt::Horizontal, parentWidget());
		m_composeHeight->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
		m_composeHeight->setRange(1, 100);
		m_composeHeight->setSliderPosition(100);
		addLabel("Compose Height");
		addWidget(m_composeHeight);
		connect(m_composeHeight, SIGNAL(valueChanged(int)), SLOT(composeChanged()));

		m_composeTop = new QSlider(Qt::Horizontal, parentWidget());
		m_composeTop->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
		m_composeTop->setRange(0, 100);
		m_composeTop->setSliderPosition(0);
		addLabel("Compose Top Offset");
		addWidget(m_composeTop);
		connect(m_composeTop, SIGNAL(valueChanged(int)), SLOT(composeChanged()));
	}

}

void GeneralTab::fixWidth()
{
	setContentsMargins(m_hMargin, m_vMargin, m_hMargin, m_vMargin);
	setColumnStretch(3, 1);

	QList<QWidget *> list = parentWidget()->findChildren<QWidget *>();
	QList<QWidget *>::iterator it;
	for (it = list.begin(); it != list.end(); ++it)	{
		if (((*it)->sizeHint().width()) > m_minWidth) {
			m_increment = (int) ceil(((*it)->sizeHint().width() - m_minWidth) / m_pxw);
			(*it)->setMinimumWidth(m_minWidth + m_increment * m_pxw); // for stepsize expansion of widgets
		}
	}

	// fix width of subgrids
	QList<QGridLayout *>::iterator i;
	for (i = m_grids.begin(); i != m_grids.end(); ++i) {
		(*i)->setColumnStretch(3, 1);
		(*i)->setContentsMargins(0, 0, 0, 0);
		for (int n = 0; n < (*i)->count(); n++) {
			if ((*i)->itemAt(n)->widget()->sizeHint().width() > m_maxw[n % 4]) {
				m_maxw[n % 4] = (*i)->itemAt(n)->widget()->sizeHint().width();
			}
			if (n % 2) {
				if (!qobject_cast<QToolButton*>((*i)->itemAt(n)->widget()))
					(*i)->itemAt(n)->widget()->setMinimumWidth(m_minWidth);
			} else {
				(*i)->itemAt(n)->widget()->setMinimumWidth(m_maxw[n % 4]);
			}
		}
		for (int j = 0; j < m_cols; j++) {
			if (j % 2)
				(*i)->setColumnMinimumWidth(j,m_maxw[j] + m_pxw);
			else
				(*i)->setColumnMinimumWidth(j,m_maxw[j]);
		}
	}

	for (int j = 0; j < m_cols; j++) {
		if (j % 2)
			setColumnMinimumWidth(j, m_maxw[j] + m_pxw);
		else
			setColumnMinimumWidth(j, m_maxw[j]);
	}
}

unsigned GeneralTab::getColorspace() const
{
	if (m_colorspace == NULL)
		return 0;
	switch (m_colorspace->currentIndex()) {
	case 0: // Autodetect
		return 0;
	case 1:
		return V4L2_COLORSPACE_SMPTE170M;
	case 2:
		return V4L2_COLORSPACE_SMPTE240M;
	case 3:
		return V4L2_COLORSPACE_REC709;
	case 4:
		return V4L2_COLORSPACE_470_SYSTEM_M;
	case 5:
		return V4L2_COLORSPACE_470_SYSTEM_BG;
	case 6:
	default:
		return V4L2_COLORSPACE_SRGB;
	}
}

unsigned GeneralTab::getDisplayColorspace() const
{
	if (m_displayColorspace == NULL)
		return V4L2_COLORSPACE_SRGB;
	switch (m_displayColorspace->currentIndex()) {
	case 0:
		return V4L2_COLORSPACE_SRGB;
	case 1: // Linear RGB
		return 0;
	case 2:
	default:
		return V4L2_COLORSPACE_REC709;
	case 3:
		return V4L2_COLORSPACE_SMPTE240M;
	}
}

void GeneralTab::setHaveBuffers(bool haveBuffers)
{
	m_haveBuffers = haveBuffers;

	if (m_videoInput)
		m_videoInput->setDisabled(haveBuffers);
	if (m_videoOutput)
		m_videoOutput->setDisabled(haveBuffers);
	if (m_tvStandard)
		m_tvStandard->setDisabled(haveBuffers);
	if (m_videoTimings)
		m_videoTimings->setDisabled(haveBuffers);
	if (m_vidCapFormats)
		m_vidCapFormats->setDisabled(haveBuffers);
	if (m_vidFields)
		m_vidFields->setDisabled(haveBuffers);
	if (m_frameSize && m_discreteSizes)
		m_frameSize->setDisabled(haveBuffers);
	if (m_frameWidth && !m_discreteSizes)
		m_frameWidth->setDisabled(haveBuffers);
	if (m_frameHeight && !m_discreteSizes)
		m_frameHeight->setDisabled(haveBuffers);
	if (m_vidOutFormats)
		m_vidOutFormats->setDisabled(haveBuffers);
	if (m_capMethods)
		m_capMethods->setDisabled(haveBuffers);
	if (m_vbiMethods)
		m_vbiMethods->setDisabled(haveBuffers);
}

void GeneralTab::showAllAudioDevices(bool use)
{
	QString oldIn(m_audioInDevice->currentText());
	QString oldOut(m_audioOutDevice->currentText());

	m_fullAudioName = use;
	if (oldIn == NULL || oldOut == NULL || !createAudioDeviceList())
		return;

	// Select a similar device as before the listings method change
	// check by comparing old selection with any matching in the new list
	bool setIn = false, setOut = false;
	int listSize = std::max(m_audioInDevice->count(), m_audioOutDevice->count());

	for (int i = 0; i < listSize; i++) {
		QString oldInCmp(oldIn.left(std::min(m_audioInDevice->itemText(i).length(), oldIn.length())));
		QString oldOutCmp(oldOut.left(std::min(m_audioOutDevice->itemText(i).length(), oldOut.length())));

		if (!setIn && i < m_audioInDevice->count()
		    && m_audioInDevice->itemText(i).startsWith(oldInCmp)) {
			setIn = true;
			m_audioInDevice->setCurrentIndex(i);
		}

		if (!setOut && i < m_audioOutDevice->count()
		    && m_audioOutDevice->itemText(i).startsWith(oldOutCmp)) {
			setOut = true;
			m_audioOutDevice->setCurrentIndex(i);
		}
	}
}

bool GeneralTab::filterAudioInDevice(QString &deviceName)
{
	// Removes S/PDIF, front speakers and surround from input devices
	// as they are output devices, not input
	if (deviceName.contains("surround")
	    || deviceName.contains("front")
	    || deviceName.contains("iec958"))
		return false;

	// Removes sysdefault too if not full audio mode listings
	if (!m_fullAudioName && deviceName.contains("sysdefault"))
		return false;

	return true;
}

bool GeneralTab::filterAudioOutDevice(QString &deviceName)
{
	// Removes advanced options if not full audio mode listings
	if (!m_fullAudioName && (deviceName.contains("surround")
				 || deviceName.contains("front")
				 || deviceName.contains("iec958")
				 || deviceName.contains("sysdefault"))) {
		return false;
	}

	return true;
}

int GeneralTab::addAudioDevice(void *hint, int deviceNum)
{
	int added = 0;
#ifdef HAVE_ALSA
	char *name;
	char *iotype;
	QString deviceName;
	QString listName;
	QStringList deviceType;
	iotype = snd_device_name_get_hint(hint, "IOID");
	name = snd_device_name_get_hint(hint, "NAME");
	deviceName.append(name);

	snd_card_get_name(deviceNum, &name);
	listName.append(name);

	deviceType = deviceName.split(":");

	// Add device io capability to list name
	if (m_fullAudioName) {
		listName.append(" ");

		// Makes the surround name more readable
		if (deviceName.contains("surround"))
			listName.append(QString("surround %1.%2")
					.arg(deviceType.value(0)[8]).arg(deviceType.value(0)[9]));
		else
			listName.append(deviceType.value(0));

	} else if (!deviceType.value(0).contains("default")) {
		listName.append(" ").append(deviceType.value(0));
	}

	// Add device number if it is not 0
	if (deviceName.contains("DEV=")) {
		int devNo;
		QStringList deviceNo = deviceName.split("DEV=");
		devNo = deviceNo.value(1).toInt();
		if (devNo)
			listName.append(QString(" %1").arg(devNo));
	}

	if ((iotype == NULL || strncmp(iotype, "Input", 5) == 0) && filterAudioInDevice(deviceName)) {
		m_audioInDevice->addItem(listName);
		m_audioInDeviceMap[listName] = snd_device_name_get_hint(hint, "NAME");
		added += AUDIO_ADD_READ;
	}

	if ((iotype == NULL || strncmp(iotype, "Output", 6) == 0)  && filterAudioOutDevice(deviceName)) {
		m_audioOutDevice->addItem(listName);
		m_audioOutDeviceMap[listName] = snd_device_name_get_hint(hint, "NAME");
		added += AUDIO_ADD_WRITE;
	}
#endif
	return added;
}

bool GeneralTab::createAudioDeviceList()
{
#ifdef HAVE_ALSA
	if (m_audioInDevice == NULL || m_audioOutDevice == NULL)
		return false;

	m_audioInDevice->clear();
	m_audioOutDevice->clear();
	m_audioInDeviceMap.clear();
	m_audioOutDeviceMap.clear();

	m_audioInDevice->addItem("None");
	m_audioOutDevice->addItem("Default");
	m_audioInDeviceMap["None"] = "None";
	m_audioOutDeviceMap["Default"] = "default";

	int deviceNum = -1;
	int audioDevices = 0;
	int matchDevice = matchAudioDevice();
	int indexDevice = -1;
	int indexCount = 0;

	while (snd_card_next(&deviceNum) >= 0) {
		if (deviceNum == -1)
			break;

		audioDevices++;
		if (deviceNum == matchDevice && indexDevice == -1)
			indexDevice = indexCount;

		void **hint;

		snd_device_name_hint(deviceNum, "pcm", &hint);
		for (int i = 0; hint[i] != NULL; i++) {
			int addAs = addAudioDevice(hint[i], deviceNum);
			if (addAs == AUDIO_ADD_READ || addAs == AUDIO_ADD_READWRITE)
				indexCount++;
		}
		snd_device_name_free_hint(hint);
	}

	snd_config_update_free_global();
	m_audioInDevice->setCurrentIndex(indexDevice + 1);
	changeAudioDevice();
	return m_audioInDeviceMap.size() > 1 && m_audioOutDeviceMap.size() > 1 && audioDevices > 1;
#else
	return false;
#endif
}

void GeneralTab::changeAudioDevice()
{
	m_audioOutDevice->setEnabled(getAudioInDevice() != NULL ? getAudioInDevice().compare("None") : false);
	emit audioDeviceChanged();
}

void GeneralTab::addWidget(QWidget *w, Qt::Alignment align)
{
	if (m_col % 2 && !qobject_cast<QToolButton*>(w))
		w->setMinimumWidth(m_minWidth);
	if (w->sizeHint().width() > m_maxw[m_col])
		m_maxw[m_col] = w->sizeHint().width();
	if (w->sizeHint().height() > m_maxh)
		m_maxh = w->sizeHint().height();
	QGridLayout::addWidget(w, m_row, m_col, align | Qt::AlignVCenter);
	m_col++;
	if (m_col == m_cols) {
		m_col = 0;
		m_row++;
	}
}

void GeneralTab::addTitle(const QString &titlename)
{
	m_row++;
	QLabel *title_info = new QLabel(titlename, parentWidget());
	QFont f = title_info->font();
	f.setBold(true);
	title_info->setFont(f);

	QGridLayout::addWidget(title_info, m_row, 0, 1, m_cols, Qt::AlignLeft);
	setRowMinimumHeight(m_row, 25);
	m_row++;

	QFrame *m_line = new QFrame(parentWidget());
	m_line->setFrameShape(QFrame::HLine);
	m_line->setFrameShadow(QFrame::Sunken);
	QGridLayout::addWidget(m_line, m_row, 0, 1, m_cols, Qt::AlignVCenter);
	m_row++;
	m_col = 0;
}

int GeneralTab::getWidth()
{
	int total = 0;
	for (int i = 0; i < m_cols; i++) {
		total += m_maxw[i] + m_pxw;
	}
	return total;
}

int GeneralTab::getHeight()
{
	return rowCount() * m_maxh;
}

bool GeneralTab::isSlicedVbi() const
{
	return m_vbiMethods && m_vbiMethods->currentText() == "Sliced";
}

CapMethod GeneralTab::capMethod()
{
	return (CapMethod)m_capMethods->itemData(m_capMethods->currentIndex()).toInt();
}

void GeneralTab::updateGUI(int input)
{
	v4l2_input in;
	enum_input(in, true, input);
	if (g_input(input) || m_isRadio) {
		m_stackedFrameSettings->hide();
		return;
	}

	if (in.capabilities & V4L2_IN_CAP_STD && in.type == V4L2_INPUT_TYPE_TUNER) {
		m_stackedFrameSettings->setCurrentIndex(0);
		m_stackedFrameSettings->show();
		m_stackedStandards->setCurrentIndex(0);
		m_stackedStandards->show();
		m_stackedFrequency->setCurrentIndex(0);
		m_stackedFrequency->show();
	} else if (in.capabilities & V4L2_IN_CAP_STD) {
		m_stackedFrameSettings->setCurrentIndex(0);
		m_stackedFrameSettings->show();
		m_stackedStandards->setCurrentIndex(0);
		m_stackedStandards->show();
		m_stackedFrequency->hide();
	} else if (in.capabilities & V4L2_IN_CAP_DV_TIMINGS) {
		m_stackedFrameSettings->setCurrentIndex(0);
		m_stackedFrameSettings->show();
		m_stackedStandards->setCurrentIndex(1);
		m_stackedStandards->show();
		m_stackedFrequency->hide();
	} else	{
		m_stackedFrameSettings->setCurrentIndex(1);
		m_stackedFrameSettings->show();
		m_stackedStandards->hide();
		m_stackedFrequency->hide();
	}

	if (isVbi()) {
		m_stackedFrameSettings->hide();
	}
}

void GeneralTab::inputChanged(int input)
{
	s_input(input);

	if (m_audioInput)
		updateAudioInput();

	updateVideoInput();
	updateVidCapFormat();
	updateGUI(input);
}

void GeneralTab::outputChanged(int output)
{
	s_output(output);
	updateVideoOutput();
	updateVidOutFormat();
}

void GeneralTab::inputAudioChanged(int input)
{
	s_audio(input);
	updateAudioInput();
}

void GeneralTab::outputAudioChanged(int output)
{
	s_audout(output);
	updateAudioOutput();
}

void GeneralTab::standardChanged(int std)
{
	v4l2_standard vs;

	enum_std(vs, true, std);
	s_std(vs.id);
	updateStandard();
}

void GeneralTab::timingsChanged(int index)
{
	v4l2_enum_dv_timings timings;

	enum_dv_timings(timings, true, index);
	s_dv_timings(timings.timings);
	updateTimings();
}

void GeneralTab::freqTableChanged(int)
{
	updateFreqChannel();
	freqChannelChanged(0);
}

void GeneralTab::freqChannelChanged(int idx)
{
	double f = v4l2_channel_lists[m_freqTable->currentIndex()].list[idx].freq;

	m_freq->setValue(f / 1000.0);
	freqChanged(m_freq->value());
}

void GeneralTab::freqChanged(double f)
{
	v4l2_frequency freq;

	if (!m_freq->isEnabled())
		return;

	g_frequency(freq);
	freq.frequency = f * m_freqFac;
	s_frequency(freq);
	updateFreq();
}

void GeneralTab::freqRfChanged(double f)
{
	v4l2_frequency freq;

	if (!m_freqRf->isEnabled())
		return;

	g_frequency(freq, 1);
	freq.frequency = f * m_freqRfFac;
	s_frequency(freq);
	updateFreqRf();
}

void GeneralTab::audioModeChanged(int)
{
	m_tuner.audmode = m_audioModes[m_audioMode->currentIndex()];
	s_tuner(m_tuner);
}

void GeneralTab::detectSubchansClicked()
{
	QString chans;

	g_tuner(m_tuner);
	if (m_tuner.rxsubchans & V4L2_TUNER_SUB_MONO)
		chans += "Mono ";
	if (m_tuner.rxsubchans & V4L2_TUNER_SUB_STEREO)
		chans += "Stereo ";
	if (m_tuner.rxsubchans & V4L2_TUNER_SUB_LANG1)
		chans += "Lang1 ";
	if (m_tuner.rxsubchans & V4L2_TUNER_SUB_LANG2)
		chans += "Lang2 ";
	if (m_tuner.rxsubchans & V4L2_TUNER_SUB_RDS)
		chans += "RDS ";
	chans += "(" + QString::number((int)(m_tuner.signal / 655.35 + 0.5)) + "%";
	if (m_tuner.signal && m_tuner.afc)
		chans += m_tuner.afc < 0 ? " too low" : " too high";
	chans += ")";

	m_subchannels->setText(chans);
	fixWidth();
}

void GeneralTab::stereoModeChanged()
{
	v4l2_modulator mod;
	bool val = m_stereoMode->checkState() == Qt::Checked;

	g_modulator(mod);
	mod.txsubchans &= ~(V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO);
	mod.txsubchans |= val ? V4L2_TUNER_SUB_STEREO : V4L2_TUNER_SUB_MONO;
	s_modulator(mod);
}

void GeneralTab::rdsModeChanged()
{
	v4l2_modulator mod;
	bool val = m_rdsMode->checkState() == Qt::Checked;

	g_modulator(mod);
	mod.txsubchans &= ~V4L2_TUNER_SUB_RDS;
	mod.txsubchans |= val ? V4L2_TUNER_SUB_RDS : 0;
	s_modulator(mod);
}

void GeneralTab::vidCapFormatChanged(int idx)
{
	v4l2_fmtdesc desc;

	enum_fmt(desc, true, idx);

	cv4l_fmt fmt;

	g_fmt(fmt);
	fmt.s_pixelformat(desc.pixelformat);
	if (try_fmt(fmt) == 0)
		s_fmt(fmt);

	updateVidCapFormat();
}

static const char *field2s(int val)
{
	switch (val) {
	case V4L2_FIELD_ANY:
		return "Any";
	case V4L2_FIELD_NONE:
		return "None";
	case V4L2_FIELD_TOP:
		return "Top";
	case V4L2_FIELD_BOTTOM:
		return "Bottom";
	case V4L2_FIELD_INTERLACED:
		return "Interlaced";
	case V4L2_FIELD_SEQ_TB:
		return "Sequential Top-Bottom";
	case V4L2_FIELD_SEQ_BT:
		return "Sequential Bottom-Top";
	case V4L2_FIELD_ALTERNATE:
		return "Alternating";
	case V4L2_FIELD_INTERLACED_TB:
		return "Interlaced Top-Bottom";
	case V4L2_FIELD_INTERLACED_BT:
		return "Interlaced Bottom-Top";
	default:
		return "";
	}
}

void GeneralTab::vidFieldChanged(int idx)
{
	cv4l_fmt fmt;

	g_fmt(fmt);
	for (__u32 f = V4L2_FIELD_NONE; f <= V4L2_FIELD_INTERLACED_BT; f++) {
		if (m_vidFields->currentText() == QString(field2s(f))) {
			fmt.s_field(f);
			s_fmt(fmt);
			break;
		}
	}
	updateVidFormat();
}

void GeneralTab::frameWidthChanged()
{
	cv4l_fmt fmt;
	int val = m_frameWidth->value();

	if (m_frameWidth->isEnabled()) {
		g_fmt(fmt);
		fmt.s_width(val);
		// Force the driver to recalculate bytesperline.
		for (unsigned p = 0; p < fmt.g_num_planes(); p++)
			fmt.s_bytesperline(0, p);
		if (try_fmt(fmt) == 0)
			s_fmt(fmt);
	}

	updateVidFormat();
}

void GeneralTab::frameHeightChanged()
{
	cv4l_fmt fmt;
	int val = m_frameHeight->value();

	if (m_frameHeight->isEnabled()) {
		g_fmt(fmt);
		fmt.s_height(val);
		if (try_fmt(fmt) == 0)
			s_fmt(fmt);
	}

	updateVidFormat();
}

void GeneralTab::frameSizeChanged(int idx)
{
	v4l2_frmsizeenum frmsize = { 0 };

	if (!enum_framesizes(frmsize, m_pixelformat, idx)) {
		cv4l_fmt fmt;

		g_fmt(fmt);
		fmt.s_width(frmsize.discrete.width);
		fmt.s_height(frmsize.discrete.height);
		// Force the driver to recalculate bytesperline.
		for (unsigned p = 0; p < fmt.g_num_planes(); p++)
			fmt.s_bytesperline(0, p);
		if (try_fmt(fmt) == 0)
			s_fmt(fmt);
	}
	updateVidFormat();
}

void GeneralTab::frameIntervalChanged(int idx)
{
	v4l2_frmivalenum frmival = { 0 };

	if (!enum_frameintervals(frmival, m_pixelformat, m_width, m_height, idx)
	    && frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
		if (!set_interval(frmival.discrete))
			m_interval = frmival.discrete;
	}
}

void GeneralTab::vidOutFormatChanged(int idx)
{
	v4l2_fmtdesc desc;

	enum_fmt(desc, true, idx);

	cv4l_fmt fmt;

	g_fmt(fmt);
	fmt.s_pixelformat(desc.pixelformat);
	if (try_fmt(fmt) == 0)
		s_fmt(fmt);
	updateVidOutFormat();
}

void GeneralTab::vbiMethodsChanged(int idx)
{
	s_type(isSlicedVbi() ? V4L2_BUF_TYPE_SLICED_VBI_CAPTURE :
				    V4L2_BUF_TYPE_VBI_CAPTURE);
}

void GeneralTab::cropChanged()
{
	v4l2_crop crop;

	if (!m_cropWidth->isEnabled())
		return;

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c.width = m_cropWidth->value();
	crop.c.left = m_cropLeft->value();
	crop.c.height = m_cropHeight->value();
	crop.c.top = m_cropTop->value();
	cv4l_ioctl(VIDIOC_S_CROP, &crop);
	updateVidCapFormat();
}

void GeneralTab::composeChanged()
{
	v4l2_selection sel;

	if (!m_composeWidth->isEnabled() || !input_has_compose())
		return;

	sel.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	sel.r.width = m_composeWidth->value();
	sel.r.left = m_composeLeft->value();
	sel.r.height = m_composeHeight->value();
	sel.r.top = m_composeTop->value();
	cv4l_ioctl(VIDIOC_S_SELECTION, &sel);
	updateVidCapFormat();
}

void GeneralTab::updateVideoInput()
{
	int input;
	v4l2_input in;

	if (g_input(input))
		return;
	enum_input(in, true, input);
	m_videoInput->setCurrentIndex(input);
	if (m_tvStandard) {
		refreshStandards();
		updateStandard();
		m_tvStandard->setEnabled(in.capabilities & V4L2_IN_CAP_STD);
		if (m_qryStandard)
			m_qryStandard->setEnabled(in.capabilities & V4L2_IN_CAP_STD);
		bool enableFreq = in.type == V4L2_INPUT_TYPE_TUNER;
		if (m_freq)
			m_freq->setEnabled(enableFreq);
		if (m_freqTable)
			m_freqTable->setEnabled(enableFreq);
		if (m_freqChannel)
			m_freqChannel->setEnabled(enableFreq);
		if (m_detectSubchans) {
			m_detectSubchans->setEnabled(enableFreq);
			if (!enableFreq) {
				m_subchannels->setText("");
				fixWidth();
			}
			else
				detectSubchansClicked();
		}
	}
	if (m_videoTimings) {
		refreshTimings();
		updateTimings();
		m_videoTimings->setEnabled(in.capabilities & V4L2_IN_CAP_DV_TIMINGS);
		if (m_qryTimings)
			m_qryTimings->setEnabled(in.capabilities & V4L2_IN_CAP_DV_TIMINGS);
	}
	if (m_audioInput)
		m_audioInput->setEnabled(in.audioset);
	if (m_cropWidth) {
		bool has_crop = input_has_crop();

		m_cropWidth->setEnabled(has_crop);
		m_cropLeft->setEnabled(has_crop);
		m_cropHeight->setEnabled(has_crop);
		m_cropTop->setEnabled(has_crop);
	}
	if (m_composeWidth) {
		bool has_compose = input_has_compose();

		m_composeWidth->setEnabled(has_compose);
		m_composeLeft->setEnabled(has_compose);
		m_composeHeight->setEnabled(has_compose);
		m_composeTop->setEnabled(has_compose);
	}
}

void GeneralTab::updateVideoOutput()
{
	int output;
	v4l2_output out;

	if (g_output(output))
		return;
	enum_output(out, true, output);
	m_videoOutput->setCurrentIndex(output);
	if (m_tvStandard) {
		refreshStandards();
		updateStandard();
		m_tvStandard->setEnabled(out.capabilities & V4L2_OUT_CAP_STD);
		if (m_qryStandard)
			m_qryStandard->setEnabled(out.capabilities & V4L2_OUT_CAP_STD);
	}
	if (m_videoTimings) {
		refreshTimings();
		updateTimings();
		m_videoTimings->setEnabled(out.capabilities & V4L2_OUT_CAP_DV_TIMINGS);
	}
}

void GeneralTab::updateAudioInput()
{
	v4l2_audio audio;
	QString what;

	g_audio(audio);
	m_audioInput->setCurrentIndex(audio.index);
	if (audio.capability & V4L2_AUDCAP_STEREO)
		what = "stereo input";
	else
		what = "mono input";
	if (audio.capability & V4L2_AUDCAP_AVL)
		what += ", has AVL";
	if (audio.mode & V4L2_AUDMODE_AVL)
		what += ", AVL is on";
	m_audioInput->setStatusTip(what);
	m_audioInput->setWhatsThis(what);
}

void GeneralTab::updateAudioOutput()
{
	v4l2_audioout audio;

	g_audout(audio);
	m_audioOutput->setCurrentIndex(audio.index);
}

void GeneralTab::refreshStandards()
{
	v4l2_standard vs;
	m_tvStandard->clear();
	if (!enum_std(vs, true)) {
		do {
			m_tvStandard->addItem((char *)vs.name);
		} while (!enum_std(vs));
	}
}

void GeneralTab::updateStandard()
{
	v4l2_std_id std;
	v4l2_standard vs;
	QString what;

	g_std(std);
	if (!enum_std(vs, true)) {
		do {
			if (vs.id == std)
				break;
		} while (!enum_std(vs));
	}
	if (vs.id != std) {
		if (!enum_std(vs, true)) {
			do {
				if (vs.id & std)
					break;
			} while (!enum_std(vs));
		}
	}
	if ((vs.id & std) == 0)
		return;
	m_tvStandard->setCurrentIndex(vs.index);
	what.sprintf("TV Standard (0x%llX)\n"
		"Frame period: %f (%d/%d)\n"
		"Frame lines: %d", (long long int)std,
		(double)vs.frameperiod.numerator / vs.frameperiod.denominator,
		vs.frameperiod.numerator, vs.frameperiod.denominator,
		vs.framelines);
	m_tvStandard->setStatusTip(what);
	m_tvStandard->setWhatsThis(what);
	updateVidFormat();
	if (!isVbi())
		changePixelAspectRatio();
}

void GeneralTab::qryStdClicked()
{
	v4l2_std_id std;

	if (!query_std(std))
		return;

	if (std == V4L2_STD_UNKNOWN) {
		info("No standard detected\n");
	} else {
		s_std(std);
		updateStandard();
	}
}

void GeneralTab::refreshTimings()
{
	v4l2_enum_dv_timings timings;
	m_videoTimings->clear();
	if (!enum_dv_timings(timings, true)) {
		do {
			v4l2_bt_timings &bt = timings.timings.bt;
			double tot_height = bt.height +
				bt.vfrontporch + bt.vsync + bt.vbackporch +
				bt.il_vfrontporch + bt.il_vsync + bt.il_vbackporch;
			double tot_width = bt.width +
				bt.hfrontporch + bt.hsync + bt.hbackporch;
			char buf[100];

			if (bt.interlaced)
				sprintf(buf, "%dx%di%.2f", bt.width, bt.height,
					(double)bt.pixelclock / (tot_width * (tot_height / 2)));
			else
				sprintf(buf, "%dx%dp%.2f", bt.width, bt.height,
					(double)bt.pixelclock / (tot_width * tot_height));
			m_videoTimings->addItem(buf);
		} while (!enum_dv_timings(timings));
	}
}

void GeneralTab::updateTimings()
{
	v4l2_dv_timings timings;
	v4l2_enum_dv_timings p;
	QString what;

	g_dv_timings(timings);
	if (!enum_dv_timings(p, true)) {
		do {
			if (!memcmp(&timings, &p.timings, sizeof(timings)))
				break;
		} while (!enum_dv_timings(p));
	}
	if (memcmp(&timings, &p.timings, sizeof(timings)))
		return;
	m_videoTimings->setCurrentIndex(p.index);
	what.sprintf("Video Timings (%u)\n"
		"Frame %ux%u\n",
		p.index, p.timings.bt.width, p.timings.bt.height);
	m_videoTimings->setStatusTip(what);
	m_videoTimings->setWhatsThis(what);
	updateVidFormat();
}

void GeneralTab::qryTimingsClicked()
{
	v4l2_dv_timings timings;

	if (!query_dv_timings(timings)) {
		s_dv_timings(timings);
		updateTimings();
	}
}

void GeneralTab::sourceChange(const v4l2_event &ev)
{
	if (!m_videoInput || (int)ev.id != m_videoInput->currentIndex())
		return;
	emit colorspaceChanged();
	if (m_qryStandard && m_qryStandard->isEnabled())
		m_qryStandard->click();
	else if (m_qryTimings && m_qryTimings->isEnabled())
		m_qryTimings->click();
}

void GeneralTab::updateFreq()
{
	v4l2_frequency f;

	g_frequency(f);
	/* m_freq listens to valueChanged block it to avoid recursion */
	m_freq->blockSignals(true);
	m_freq->setValue((double)f.frequency / m_freqFac);
	m_freq->blockSignals(false);
}

void GeneralTab::updateFreqChannel()
{
	m_freqChannel->clear();
	int tbl = m_freqTable->currentIndex();
	const struct v4l2_channel_list *list = v4l2_channel_lists[tbl].list;
	for (unsigned i = 0; i < v4l2_channel_lists[tbl].count; i++)
		m_freqChannel->addItem(list[i].name);
}

void GeneralTab::updateFreqRf()
{
	v4l2_frequency f;

	g_frequency(f, 1);
	/* m_freqRf listens to valueChanged block it to avoid recursion */
	m_freqRf->blockSignals(true);
	m_freqRf->setValue((double)f.frequency / m_freqRfFac);
	m_freqRf->blockSignals(false);
}

void GeneralTab::updateVidCapFormat()
{
	v4l2_fmtdesc desc;
	cv4l_fmt fmt;

	if (isVbi())
		return;
	g_fmt(fmt);
	m_pixelformat = fmt.g_pixelformat();
	m_width = fmt.g_width();
	m_height = fmt.g_height();
	updateFrameSize();
	updateFrameInterval();
	if (!enum_fmt(desc, true)) {
		do {
			if (desc.pixelformat == m_pixelformat)
				break;
		} while (!enum_fmt(desc));
	}
	if (desc.pixelformat != m_pixelformat)
		return;
	m_vidCapFormats->setCurrentIndex(desc.index);
	updateVidFields();
	updateCrop();
	updateCompose();
}

void GeneralTab::updateVidOutFormat()
{
	v4l2_fmtdesc desc;
	cv4l_fmt fmt;

	g_fmt(fmt);
	m_pixelformat = fmt.g_pixelformat();
	m_width = fmt.g_width();
	m_height = fmt.g_height();
	updateFrameSize();
	updateFrameInterval();
	if (!enum_fmt(desc, true)) {
		do {
			if (desc.pixelformat == m_pixelformat)
				break;
		} while (!enum_fmt(desc));
	}
	if (desc.pixelformat == m_pixelformat)
		return;
	m_vidOutFormats->setCurrentIndex(desc.index);
	updateVidFields();
}

void GeneralTab::updateVidFields()
{
	cv4l_fmt fmt;
	cv4l_fmt tmp;
	bool first = true;

	g_fmt(fmt);

	for (__u32 f = V4L2_FIELD_NONE; f <= V4L2_FIELD_INTERLACED_BT; f++) {
		tmp = fmt;
		tmp.s_field(f);
		if (try_fmt(tmp) || tmp.g_field() != f)
			continue;
		if (first) {
			m_vidFields->clear();
			first = false;
		}
		m_vidFields->addItem(field2s(f));
		if (fmt.g_field() == f)
			m_vidFields->setCurrentIndex(m_vidFields->count() - 1);
	}
}

void GeneralTab::updateCrop()
{
	if (m_cropWidth == NULL || !m_cropWidth->isEnabled())
		return;

	v4l2_cropcap cropcap;
	v4l2_rect &b = cropcap.bounds;
	v4l2_crop crop;
	v4l2_rect &c = crop.c;

	cropcap.type = crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (cv4l_ioctl(VIDIOC_CROPCAP, &cropcap) ||
	    cv4l_ioctl(VIDIOC_G_CROP, &crop))
		return;

	m_cropWidth->blockSignals(true);
	m_cropLeft->blockSignals(true);
	m_cropHeight->blockSignals(true);
	m_cropTop->blockSignals(true);

	m_cropWidth->setRange(8, b.width);
	m_cropWidth->setSliderPosition(c.width);
	if (b.width != c.width) {
		m_cropLeft->setRange(b.left, b.left + b.width - c.width);
		m_cropLeft->setSliderPosition(c.left);
	}
	m_cropHeight->setRange(8, b.height);
	m_cropHeight->setSliderPosition(c.height);
	if (b.height != c.height) {
		m_cropTop->setRange(b.top, b.top + b.height - c.height);
		m_cropTop->setSliderPosition(c.top);
	}

	m_cropWidth->blockSignals(false);
	m_cropLeft->blockSignals(false);
	m_cropHeight->blockSignals(false);
	m_cropTop->blockSignals(false);
}

void GeneralTab::updateCompose()
{
	if (m_composeWidth == NULL || !m_composeWidth->isEnabled())
		return;

	v4l2_selection sel;
	v4l2_rect &r = sel.r;

	sel.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	if (cv4l_ioctl(VIDIOC_G_SELECTION, &sel))
		return;

	m_composeWidth->blockSignals(true);
	m_composeLeft->blockSignals(true);
	m_composeHeight->blockSignals(true);
	m_composeTop->blockSignals(true);

	m_composeWidth->setRange(8, m_width);
	m_composeWidth->setSliderPosition(r.width);
	if (m_width != r.width) {
		m_composeLeft->setRange(0, m_width - r.width);
		m_composeLeft->setSliderPosition(r.left);
	}
	m_composeHeight->setRange(8, m_height);
	m_composeHeight->setSliderPosition(r.height);
	if (m_height != r.height) {
		m_composeTop->setRange(0, m_height - r.height);
		m_composeTop->setSliderPosition(r.top);
	}

	m_composeWidth->blockSignals(false);
	m_composeLeft->blockSignals(false);
	m_composeHeight->blockSignals(false);
	m_composeTop->blockSignals(false);
	emit clearBuffers();
}

void GeneralTab::updateFrameSize()
{
	v4l2_frmsizeenum frmsize;
	bool ok = false;

	m_frameSize->clear();

	ok = !enum_framesizes(frmsize, m_pixelformat);
	if (ok && frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
		do {
			m_frameSize->addItem(QString("%1x%2")
				.arg(frmsize.discrete.width).arg(frmsize.discrete.height));
			if (frmsize.discrete.width == m_width &&
			    frmsize.discrete.height == m_height)
				m_frameSize->setCurrentIndex(frmsize.index);
		} while (!enum_framesizes(frmsize));

		m_discreteSizes = true;
		m_frameWidth->setEnabled(false);
		m_frameWidth->blockSignals(true);
		m_frameWidth->setMinimum(m_width);
		m_frameWidth->setMaximum(m_width);
		m_frameWidth->setValue(m_width);
		m_frameWidth->blockSignals(false);

		m_frameHeight->setEnabled(false);
		m_frameHeight->blockSignals(true);
		m_frameHeight->setMinimum(m_height);
		m_frameHeight->setMaximum(m_height);
		m_frameHeight->setValue(m_height);
		m_frameHeight->blockSignals(false);
		m_frameSize->setEnabled(!m_haveBuffers);
		updateFrameInterval();
		return;
	}
	if (!ok) {
		frmsize.stepwise.min_width = 8;
		frmsize.stepwise.max_width = 4096;
		frmsize.stepwise.step_width = 1;
		frmsize.stepwise.min_height = 8;
		frmsize.stepwise.max_height = 2160;
		frmsize.stepwise.step_height = 1;
	}
	m_discreteSizes = false;
	m_frameSize->setEnabled(false);
	m_frameWidth->setEnabled(!m_haveBuffers);
	m_frameWidth->blockSignals(true);
	m_frameWidth->setMinimum(frmsize.stepwise.min_width);
	m_frameWidth->setMaximum(frmsize.stepwise.max_width);
	m_frameWidth->setSingleStep(frmsize.stepwise.step_width);
	m_frameWidth->setValue(m_width);
	m_frameWidth->blockSignals(false);

	m_frameHeight->setEnabled(!m_haveBuffers);
	m_frameHeight->blockSignals(true);
	m_frameHeight->setMinimum(frmsize.stepwise.min_height);
	m_frameHeight->setMaximum(frmsize.stepwise.max_height);
	m_frameHeight->setSingleStep(frmsize.stepwise.step_height);
	m_frameHeight->setValue(m_height);
	m_frameHeight->blockSignals(false);
	updateFrameInterval();
}

CropMethod GeneralTab::getCropMethod()
{
	switch (m_cropping->currentIndex()) {
	case 1:
		return QV4L2_CROP_TB;
	case 2:
		return QV4L2_CROP_P43;
	case 3:
		return QV4L2_CROP_W149;
	case 4:
		return QV4L2_CROP_W169;
	case 5:
		return QV4L2_CROP_C185;
	case 6:
		return QV4L2_CROP_C239;
	default:
		return QV4L2_CROP_NONE;
	}
}

void GeneralTab::changePixelAspectRatio()
{
	// Update hints by calling a get
	getPixelAspectRatio();
	info("");
	emit pixelAspectRatioChanged();
}

double GeneralTab::getPixelAspectRatio()
{
	v4l2_fract ratio = { 1, 1 };
	unsigned w = 0, h = 0;

	ratio = g_pixel_aspect(w, h);
	switch (m_pixelAspectRatio->currentIndex()) {
	// override ratio if hardcoded, but keep w and h
	case 1:
		ratio.numerator = 1;
		ratio.denominator = 1;
		break;
	case 2:
		ratio.numerator = 11;
		ratio.denominator = 10;
		break;
	case 3:
		ratio.numerator = 33;
		ratio.denominator = 40;
		break;
	case 4:
		ratio.numerator = 11;
		ratio.denominator = 12;
		break;
	case 5:
		ratio.numerator = 11;
		ratio.denominator = 16;
		break;
	default:
		break;
	}

	m_pixelAspectRatio->setWhatsThis(QString("Pixel Aspect Ratio y:x = %1:%2")
			 .arg(ratio.numerator).arg(ratio.denominator));
	m_pixelAspectRatio->setStatusTip(m_pixelAspectRatio->whatsThis());

	cv4l_fmt fmt;
	unsigned cur_width, cur_height;
	unsigned cur_field;

	g_fmt(fmt);

	cur_width = fmt.g_width();
	cur_height = fmt.g_height();
	cur_field = fmt.g_field();
	if (w == 0)
		w = cur_width;
	if (cur_field == V4L2_FIELD_TOP ||
	    cur_field == V4L2_FIELD_BOTTOM ||
	    cur_field == V4L2_FIELD_ALTERNATE) {
		// If we only capture a single field, then each pixel is twice
		// as high and the default image height is half the reported
		// height.
		ratio.numerator *= 2;
		h /= 2;
	}
	if (h == 0)
		h = cur_height;

	// Note: ratio is y / x, whereas we want x / y, so we return
	// denominator / numerator.
	// In addition, the ratio is for the unscaled image (i.e., the default
	// image rectangle as returned by VIDIOC_CROPCAP). So we have to
	// compensate for the current scaling factor.
	return (((double)ratio.denominator * w) / cur_width) /
	       (((double)ratio.numerator * h) / cur_height);
}

void GeneralTab::updateFrameInterval()
{
	v4l2_frmivalenum frmival = { 0 };
	v4l2_fract curr = { 1, 1 };
	bool curr_ok, ok;

	m_frameInterval->clear();

	ok = !enum_frameintervals(frmival, m_pixelformat, m_width, m_height);
	m_has_interval = ok && frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE;
	m_frameInterval->setEnabled(m_has_interval);
	if (m_has_interval) {
	        m_interval = frmival.discrete;
        	curr_ok = !cv4l_fd::get_interval(curr);
		do {
			m_frameInterval->addItem(QString("%1 fps")
				.arg((double)frmival.discrete.denominator / frmival.discrete.numerator));
			if (curr_ok &&
			    frmival.discrete.numerator == curr.numerator &&
			    frmival.discrete.denominator == curr.denominator) {
				m_frameInterval->setCurrentIndex(frmival.index);
				m_interval = frmival.discrete;
                        }
		} while (!enum_frameintervals(frmival));
	}
}

bool GeneralTab::get_interval(struct v4l2_fract &interval)
{
	if (m_has_interval)
		interval = m_interval;

	return m_has_interval;
}

QString GeneralTab::getAudioInDevice()
{
	if (m_audioInDevice == NULL)
		return NULL;

	return m_audioInDeviceMap[m_audioInDevice->currentText()];
}

QString GeneralTab::getAudioOutDevice()
{
	if (m_audioOutDevice == NULL)
		return NULL;

	return m_audioOutDeviceMap[m_audioOutDevice->currentText()];
}

void GeneralTab::setAudioDeviceBufferSize(int size)
{
	m_audioDeviceBufferSize = size;
}

int GeneralTab::getAudioDeviceBufferSize()
{
	return m_audioDeviceBufferSize;
}

#ifdef HAVE_ALSA
int GeneralTab::checkMatchAudioDevice(void *md, const char *vid, const enum device_type type)
{
	const char *devname = NULL;

	while ((devname = get_associated_device(md, devname, type, vid, MEDIA_V4L_VIDEO)) != NULL) {
		if (type == MEDIA_SND_CAP) {
			QStringList devAddr = QString(devname).split(QRegExp("[:,]"));
			return devAddr.value(1).toInt();
		}
	}
	return -1;
}

int GeneralTab::matchAudioDevice()
{
	QStringList devPath = m_device.split("/");
	QString curDev = devPath.value(devPath.count() - 1);

	void *media;
	const char *video = NULL;
	int match;

	media = discover_media_devices();

	while ((video = get_associated_device(media, video, MEDIA_V4L_VIDEO, NULL, NONE)) != NULL)
		if (curDev.compare(video) == 0)
			for (int i = 0; i <= MEDIA_SND_HW; i++)
				if ((match = checkMatchAudioDevice(media, video, static_cast<device_type>(i))) != -1)
					return match;

	return -1;
}
#endif

bool GeneralTab::hasAlsaAudio()
{
#ifdef HAVE_ALSA
	return !isVbi();
#else
	return false;
#endif
}
