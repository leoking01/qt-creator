/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "commandbuilderaspect.h"

#include "cmakecommandbuilder.h"
#include "incredibuildconstants.h"
#include "makecommandbuilder.h"

#include <projectexplorer/abstractprocessstep.h>

#include <utils/environment.h>
#include <utils/pathchooser.h>

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

using namespace ProjectExplorer;
using namespace Utils;

namespace IncrediBuild {
namespace Internal {

class CommandBuilderAspectPrivate
{
public:
    CommandBuilderAspectPrivate(BuildStep *step)
        : m_buildStep{step},
          m_customCommandBuilder{step},
          m_makeCommandBuilder{step},
          m_cmakeCommandBuilder{step}
     {}

    void tryToMigrate();
    void setActiveCommandBuilder(const QString &commandBuilderId);

    BuildStep *m_buildStep;
    CommandBuilder m_customCommandBuilder;
    MakeCommandBuilder m_makeCommandBuilder;
    CMakeCommandBuilder m_cmakeCommandBuilder;

    CommandBuilder *m_commandBuilders[3] {
        &m_customCommandBuilder,
        &m_makeCommandBuilder,
        &m_cmakeCommandBuilder
    };

    // Default to the first in list, which should be the "Custom Command"
    CommandBuilder *m_activeCommandBuilder = m_commandBuilders[0];

    bool m_loadedFromMap = false;

    QPointer<QLabel> label;
    QPointer<QComboBox> commandBuilder;
    QPointer<PathChooser> makePathChooser;
    QPointer<QLineEdit> makeArgumentsLineEdit;
};

CommandBuilderAspect::CommandBuilderAspect(BuildStep *step)
    : d(new CommandBuilderAspectPrivate(step))
{
}

CommandBuilderAspect::~CommandBuilderAspect()
{
    delete d;
}

QString CommandBuilderAspect::fullCommandFlag(bool keepJobNum) const
{
    QString argsLine = d->m_activeCommandBuilder->arguments();

    if (!keepJobNum)
        argsLine = d->m_activeCommandBuilder->setMultiProcessArg(argsLine);

    QString fullCommand("\"%0\" %1");
    fullCommand = fullCommand.arg(d->m_activeCommandBuilder->command(), argsLine);

    return fullCommand;
}

void CommandBuilderAspectPrivate::setActiveCommandBuilder(const QString &commandBuilderId)
{
    for (CommandBuilder *p : m_commandBuilders) {
        if (p->id() == commandBuilderId) {
            m_activeCommandBuilder = p;
            break;
        }
    }
}

void CommandBuilderAspectPrivate::tryToMigrate()
{
    // This constructor is called when creating a fresh build step.
    // Attempt to detect build system from pre-existing steps.
    for (CommandBuilder *p : m_commandBuilders) {
        if (p->canMigrate(m_buildStep->stepList())) {
            m_activeCommandBuilder = p;
            break;
        }
    }
}

void CommandBuilderAspect::addToLayout(LayoutBuilder &builder)
{
    if (!d->commandBuilder) {
        d->commandBuilder = new QComboBox;
        for (CommandBuilder *p : d->m_commandBuilders)
            d->commandBuilder->addItem(p->displayName());
        connect(d->commandBuilder, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
            if (idx >= 0 && idx < int(sizeof(d->m_commandBuilders) / sizeof(d->m_commandBuilders[0])))
                d->m_activeCommandBuilder = d->m_commandBuilders[idx];
            updateGui();
        });
    }

    if (!d->makePathChooser) {
        d->makePathChooser = new PathChooser;
        d->makePathChooser->setExpectedKind(PathChooser::Kind::ExistingCommand);
        d->makePathChooser->setBaseDirectory(FilePath::fromString(PathChooser::homePath()));
        d->makePathChooser->setHistoryCompleter("IncrediBuild.BuildConsole.MakeCommand.History");
        connect(d->makePathChooser, &PathChooser::rawPathChanged, this, [this] {
            d->m_activeCommandBuilder->setCommand(d->makePathChooser->rawPath());
            updateGui();
        });
    }

   if (!d->makeArgumentsLineEdit) {
        d->makeArgumentsLineEdit = new QLineEdit;
        connect(d->makeArgumentsLineEdit, &QLineEdit::textEdited, this, [this](const QString &arg) {
            d->m_activeCommandBuilder->setArguments(arg);
            updateGui();
        });
    }

    if (!d->label) {
        d->label = new QLabel(tr("Command Helper:"));
        d->label->setToolTip(tr("Select an helper to establish the build command."));
    }

    // On first creation of the step, attempt to detect and migrate from preceding steps
    if (!d->m_loadedFromMap)
        d->tryToMigrate();

    builder.startNewRow().addItems(d->label.data(), d->commandBuilder.data());
    builder.startNewRow().addItems(tr("Make command:"), d->makePathChooser.data());
    builder.startNewRow().addItems(tr("Make arguments:"), d->makeArgumentsLineEdit.data());

    updateGui();
}

void CommandBuilderAspect::fromMap(const QVariantMap &map)
{
    d->m_loadedFromMap = true;

    d->setActiveCommandBuilder(map.value(settingsKey()).toString());
    d->m_customCommandBuilder.fromMap(map);
    d->m_makeCommandBuilder.fromMap(map);
    d->m_cmakeCommandBuilder.fromMap(map);

    updateGui();
}

void CommandBuilderAspect::toMap(QVariantMap &map) const
{
    map[IncrediBuild::Constants::INCREDIBUILD_BUILDSTEP_TYPE]
            = QVariant(IncrediBuild::Constants::BUILDCONSOLE_BUILDSTEP_ID);
    map[settingsKey()] = QVariant(d->m_activeCommandBuilder->id());

    d->m_customCommandBuilder.toMap(&map);
    d->m_makeCommandBuilder.toMap(&map);
    d->m_cmakeCommandBuilder.toMap(&map);
}

void CommandBuilderAspect::updateGui()
{
    if (!d->commandBuilder)
        return;

    d->commandBuilder->setCurrentText(d->m_activeCommandBuilder->displayName());

    const QString defaultCommand = d->m_activeCommandBuilder->defaultCommand();
    d->makePathChooser->lineEdit()->setPlaceholderText(defaultCommand);
    d->makePathChooser->setPath(d->m_activeCommandBuilder->command());

    const QString defaultArgs = d->m_activeCommandBuilder->defaultArguments();
    d->makeArgumentsLineEdit->setPlaceholderText(defaultArgs);
    d->makeArgumentsLineEdit->setText(d->m_activeCommandBuilder->arguments());
}

// TextDisplay

void TextDisplay::addToLayout(LayoutBuilder &builder)
{
    if (!m_label) {
        m_label = new QLabel(m_message);
        m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    builder.addItem(m_label.data());
}

} // namespace Internal
} // namespace IncrediBuild
