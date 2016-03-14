#include <qwidget.h>
#include "samplingthread.h"

class Plot;
class Knob;
class WheelBox;
class QLabel;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    MainWindow(QWidget * = NULL);

    void initialize(int argc, char **argv);

    void start();
    void stop();
    void wait(int t_ms);

    double yscale() const;
    double yoffset() const;
    double signalInterval() const;

Q_SIGNALS:
    void amplitudeChanged(double);
    void frequencyChanged(double);
    void signalIntervalChanged(double);

private:
    Knob *d_yoffsetKnob;
    Knob *d_yscaleKnob;
    WheelBox *d_timerWheel;
    WheelBox *d_intervalWheel;
    WheelBox *d_fineoffsetWheel;

    QLabel *d_infoLabel;

    Plot *d_plot;
    std::unique_ptr<SamplingThread> sampler;
};
