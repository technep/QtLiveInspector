// widgetdemo — pure Qt Widgets target for QtLiveInspector. Mirrors the
// quickdemo surface (counter button, slider, line edit, combobox, settings
// dialog) so the probe's widget paths can be exercised: widget overlay
// input visualization, animateClick, widget screenshots and highlighting.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setObjectName("mainWindow");
    window.setWindowTitle("WidgetDemo — QtLiveInspector target");
    window.resize(680, 520);

    auto *layout = new QVBoxLayout(&window);

    auto *buttonRow = new QHBoxLayout;
    auto *clickButton = new QPushButton("Clicked 0 times", &window);
    clickButton->setObjectName("clickButton");
    auto *resetButton = new QPushButton("Reset", &window);
    resetButton->setObjectName("resetButton");
    auto *featureCheck = new QCheckBox("Feature enabled", &window);
    featureCheck->setObjectName("featureCheck");
    buttonRow->addWidget(clickButton);
    buttonRow->addWidget(resetButton);
    buttonRow->addWidget(featureCheck);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    auto *volumeRow = new QHBoxLayout;
    auto *volumeLabelCaption = new QLabel("Volume:", &window);
    auto *volumeSlider = new QSlider(Qt::Horizontal, &window);
    volumeSlider->setObjectName("volumeSlider");
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(40);
    auto *volumeLabel = new QLabel("40", &window);
    volumeLabel->setObjectName("volumeLabel");
    volumeRow->addWidget(volumeLabelCaption);
    volumeRow->addWidget(volumeSlider, 1);
    volumeRow->addWidget(volumeLabel);
    layout->addLayout(volumeRow);

    auto *nameField = new QLineEdit(&window);
    nameField->setObjectName("nameField");
    nameField->setPlaceholderText("Type something here");
    layout->addWidget(nameField);

    auto *comboRow = new QHBoxLayout;
    auto *fruitLabel = new QLabel("Fruit:", &window);
    auto *fruitCombo = new QComboBox(&window);
    fruitCombo->setObjectName("fruitCombo");
    fruitCombo->addItems({"Apple", "Banana", "Cherry", "Dragonfruit"});
    comboRow->addWidget(fruitLabel);
    comboRow->addWidget(fruitCombo, 1);
    layout->addLayout(comboRow);

    auto *statusLabel = new QLabel("last action: none", &window);
    statusLabel->setObjectName("statusLabel");
    layout->addWidget(statusLabel);

    auto *settingsButton = new QPushButton("Settings", &window);
    settingsButton->setObjectName("settingsButton");
    layout->addWidget(settingsButton);
    layout->addStretch();

    QDialog settingsDialog(&window);
    settingsDialog.setObjectName("settingsDialog");
    settingsDialog.setWindowTitle("Settings");
    settingsDialog.setModal(true);
    auto *dialogLayout = new QVBoxLayout(&settingsDialog);
    auto *dialogCaption = new QLabel("Dialog name:", &settingsDialog);
    auto *dialogNameField = new QLineEdit(&settingsDialog);
    dialogNameField->setObjectName("dialogNameField");
    dialogNameField->setPlaceholderText("Type inside the dialog");
    // Plain buttons rather than QDialogButtonBox: with the offscreen
    // platform, the button box's standard buttons did not propagate clicks
    // to the dialog (its clicked -> accepted chain never fired, even for a
    // direct click() slot call) — worth a Qt bug report, but not what this
    // demo is here to test.
    auto *dialogButtons = new QHBoxLayout;
    auto *dialogOkButton = new QPushButton("OK", &settingsDialog);
    dialogOkButton->setObjectName("dialogOkButton");
    auto *dialogCancelButton = new QPushButton("Cancel", &settingsDialog);
    dialogCancelButton->setObjectName("dialogCancelButton");
    dialogButtons->addStretch();
    dialogButtons->addWidget(dialogOkButton);
    dialogButtons->addWidget(dialogCancelButton);
    dialogLayout->addLayout(dialogButtons);
    dialogLayout->addWidget(dialogCaption);
    dialogLayout->addWidget(dialogNameField);

    int clickCount = 0;
    auto updateClickText = [&] {
        clickButton->setText(QStringLiteral("Clicked %1 times").arg(clickCount));
    };

    QObject::connect(clickButton, &QPushButton::clicked, [&] {
        ++clickCount;
        updateClickText();
    });
    QObject::connect(resetButton, &QPushButton::clicked, [&] {
        clickCount = 0;
        updateClickText();
    });
    QObject::connect(volumeSlider, &QSlider::valueChanged, [&] {
        volumeLabel->setText(QString::number(volumeSlider->value()));
    });
    QObject::connect(settingsButton, &QPushButton::clicked, [&] {
        settingsDialog.open();
    });
    QObject::connect(dialogOkButton, &QPushButton::clicked, &settingsDialog,
                     &QDialog::accept);
    QObject::connect(dialogCancelButton, &QPushButton::clicked, &settingsDialog,
                     &QDialog::reject);
    QObject::connect(&settingsDialog, &QDialog::accepted, [&] {
        statusLabel->setText("last action: dialog:accepted");
    });
    QObject::connect(&settingsDialog, &QDialog::rejected, [&] {
        statusLabel->setText("last action: dialog:rejected");
    });
    QObject::connect(fruitCombo, &QComboBox::currentTextChanged, [&] {
        statusLabel->setText("last action: combo:" + fruitCombo->currentText());
    });

    window.show();
    return app.exec();
}
