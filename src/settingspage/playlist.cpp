#include "../dialog/settingsdialog.hpp"

QWidget *SettingsDialog::playlistSettings()
{
	auto layout = new QVBoxLayout();
	layout->setAlignment(Qt::AlignTop);
	layout->setSpacing(8);

	plHints = QStringList(
		{
			"Display in the order they are fetched.",
			"Display in alphabetical order from A to Z.",
			"Display in order of most recently edited.",
			"Display in a custom order defined below."
		}
	);

	auto typeContainer = new QHBoxLayout();
	typeContainer->addWidget(new QLabel("Playlist order"));
	plOrder = new QComboBox(this);
	plOrder->addItems(
		{
			"Default", "Alphabetical", "Recent", "Custom"
		}
	);
	plOrder->setCurrentIndex(settings.general.playlistOrder);
	typeContainer->addWidget(plOrder);
	layout->addLayout(typeContainer);

	plHint = new QLabel(plHints.at(plOrder->currentIndex()), this);
	plHint->setWordWrap(true);
	layout->addWidget(plHint);

	QComboBox::connect(
		plOrder, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SettingsDialog::playlistItemChanged);

	auto mainWindow = dynamic_cast<MainWindow *>(parentWidget());

	plListLayout = new QHBoxLayout();
	plList = new QListWidget(this);
	plListLayout->addWidget(plList, 1);

	auto buttons = new QToolBar(this);
	buttons->setOrientation(Qt::Vertical);

	QAction::connect(
		buttons->addAction(Icon::get("go-up"), "Up"),
		&QAction::triggered, this, &SettingsDialog::playlistUp);
	QAction::connect(
		buttons->addAction(Icon::get("go-down"), "Down"),
		&QAction::triggered, this, &SettingsDialog::playlistDown);

	plListLayout->addWidget(buttons);
	layout->addLayout(plListLayout, 1);
	playlistItemChanged(plOrder->currentIndex());

	auto mainList = mainWindow->getPlaylistsList();
	for (auto i = 0; i < mainList->count(); i++)
	{
		auto mainItem = mainList->item(i);
		auto listItem = new QListWidgetItem(mainItem->text());
		listItem->setData(DataRole::RolePlaylistId, mainItem->data(DataRole::RolePlaylistId));
		plList->addItem(listItem);
	}

	// Final layout
	auto widget = new QWidget();
	widget->setLayout(layout);
	return widget;
}

void SettingsDialog::playlistItemChanged(int index)
{
	for (auto i = 0; i < plListLayout->count(); i++)
		plListLayout->itemAt(i)->widget()->setEnabled(index == 3);

	plHint->setText(plHints.at(index));
}

void SettingsDialog::playlistMove(int steps)
{
	auto row = plList->currentRow();
	auto item = plList->takeItem(row);
	plList->insertItem(row + steps, item);
	plList->setCurrentItem(item);
}

void SettingsDialog::playlistUp()
{
	playlistMove(-1);
}

void SettingsDialog::playlistDown()
{
	playlistMove(1);
}