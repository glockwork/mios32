/* -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*- */
// $Id: UploadWindow.h 1050 2010-06-13 16:34:08Z tk $
/*
 * Upload Window Component
 *
 * ==========================================================================
 *
 *  Copyright (C) 2010 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _UPLOAD_WINDOW_H
#define _UPLOAD_WINDOW_H

#include "../includes.h"
#include "../HexFileLoader.h"
#include "../SysexHelper.h"
#include "../UploadHandler.h"
#include "LogBox.h"

class MiosStudio; // forward declaration


class UploadWindow
    : public Component
    , public ButtonListener
    , public SliderListener
    , public FilenameComponentListener
    , public MultiTimer
{
public:
    //==============================================================================
    UploadWindow(MiosStudio *_miosStudio);
    ~UploadWindow();

    //==============================================================================
    void paint (Graphics& g);
    void resized();
    void buttonClicked (Button* buttonThatWasClicked);
    void sliderValueChanged(Slider* slider);
    void filenameComponentChanged(FilenameComponent *fileComponentThatHasChanged);

    //==============================================================================
    void handleIncomingMidiMessage(const MidiMessage& message, uint8 runningStatus);

    void midiPortChanged(void);
    void queryCore(void);

    //==============================================================================
    void uploadStart(void);
    void uploadStop(void);

    //==============================================================================
#if JUCE_MAJOR_VERSION==1 && JUCE_MINOR_VERSION<51
    void timerCallback(const int timerId);
#else
    void timerCallback(int timerId);
#endif
protected:
    //==============================================================================
    FilenameComponent* fileChooser;
    TextButton* queryButton;
    Label*      deviceIdLabel;
    Slider*     deviceIdSlider;
    LogBox* uploadStatus;
    LogBox* uploadQuery;
    TextButton* startButton;
    TextButton* stopButton;
    ProgressBar* progressBar;

    //==============================================================================
    MiosStudio *miosStudio;

    //==============================================================================
    double progress;
    bool waitUploadRequestMessagePrint;

    //==============================================================================
    // (prevent copy constructor and operator= being generated..)
    UploadWindow (const UploadWindow&);
    const UploadWindow& operator= (const UploadWindow&);
};

#endif /* _UPLOAD_WINDOW_H */
