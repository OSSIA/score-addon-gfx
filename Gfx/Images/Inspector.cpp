#include "Inspector.hpp"

#include <score/document/DocumentContext.hpp>

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace Gfx::Images
{
InspectorWidget::InspectorWidget(
    const Gfx::Images::Model& object,
    const score::DocumentContext& context,
    QWidget* parent)
    : InspectorWidgetDelegate_T{object, parent}
    , m_dispatcher{context.commandStack}
{
  auto lay = new QFormLayout{this};
  /*
  auto edit = new QLineEdit{object.path(), this};
  lay->addRow(tr("Path"), edit);

  connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
    this->m_dispatcher.submit<ChangeImages>(this->process(), edit->text());
  });*/
}

InspectorWidget::~InspectorWidget() {}
}
