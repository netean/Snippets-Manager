#include "quicksearch.h"
#include "clipboard.h"
#include "database.h"
#include "snippetmodel.h"
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

// Draws each snippet as its title with the first line of its content below
class SnippetDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        QRect rect = opt.rect.adjusted(8, 4, -8, -4);
        bool selected = opt.state & QStyle::State_Selected;
        QPalette::ColorGroup group = opt.state & QStyle::State_Active ? QPalette::Active : QPalette::Inactive;
        QColor textColor = opt.palette.color(group, selected ? QPalette::HighlightedText : QPalette::Text);
        QColor dimColor = textColor;
        dimColor.setAlphaF(0.65f);

        painter->save();
        QFont titleFont = opt.font;
        titleFont.setBold(true);
        QFontMetrics titleMetrics(titleFont);
        painter->setFont(titleFont);
        painter->setPen(textColor);
        QString title = titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, rect.width());
        painter->drawText(rect.left(), rect.top() + titleMetrics.ascent(), title);

        QFontMetrics metrics(opt.font);
        painter->setFont(opt.font);
        painter->setPen(dimColor);
        QString preview = metrics.elidedText(index.data(SnippetModel::PreviewRole).toString(), Qt::ElideRight, rect.width());
        painter->drawText(rect.left(), rect.top() + titleMetrics.height() + 2 + metrics.ascent(), preview);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        QFont titleFont = option.font;
        titleFont.setBold(true);
        size.setHeight(QFontMetrics(titleFont).height() + QFontMetrics(option.font).height() + 10);
        return size;
    }
};

QuickSearch::QuickSearch(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setWindowTitle("Snippet Manager - Quick Search");
    setObjectName("quickSearch");
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(560, 400);

    m_search = new QLineEdit;
    m_search->setObjectName("quickSearchEdit");
    m_search->setPlaceholderText("Search snippets...");
    m_search->setClearButtonEnabled(true);
    QFont searchFont = m_search->font();
    searchFont.setPointSizeF(searchFont.pointSizeF() * 1.25);
    m_search->setFont(searchFont);
    m_search->installEventFilter(this);

    m_model = new SnippetModel(this);
    m_model->setFlat(true);
    m_list = new QListView;
    m_list->setObjectName("quickSearchList");
    m_list->setModel(m_model);
    m_list->setItemDelegate(new SnippetDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setFocusPolicy(Qt::NoFocus);  // typing always goes to the search box

    m_hint = new QLabel("Enter: copy    Ctrl+Enter: open in Snippet Manager    Esc: close");
    m_hint->setAlignment(Qt::AlignCenter);
    QPalette hintPalette = m_hint->palette();
    hintPalette.setColor(QPalette::WindowText, hintPalette.color(QPalette::PlaceholderText));
    m_hint->setPalette(hintPalette);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_hint);

    // A frame, since the window has no title bar
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("QuickSearch#quickSearch { border: 1px solid palette(mid); }");

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model->search(text);
        if (m_model->rowCount() > 0) {
            m_list->setCurrentIndex(m_model->index(0, 0));
        }
    });
    connect(m_list, &QListView::activated, this, &QuickSearch::copyCurrent);
    connect(m_list, &QListView::doubleClicked, this, &QuickSearch::copyCurrent);
}

void QuickSearch::popup(const QString &activationToken)
{
#ifdef Q_OS_LINUX
    // On Wayland, a window only gets the focus with a token from whatever
    // started it; Qt picks this up when the window is shown
    if (!activationToken.isEmpty()) {
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    }
#else
    Q_UNUSED(activationToken)
#endif

    // The snippets may have changed since last time
    m_search->clear();
    m_model->refresh();
    if (m_model->rowCount() > 0) {
        m_list->setCurrentIndex(m_model->index(0, 0));
    }

    // Centre on the screen with the mouse (ignored on Wayland, where the desktop places it)
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect area = screen->availableGeometry();
        move(area.center() - QPoint(width() / 2, height() / 2));
    }

    show();
    raise();
    activateWindow();
    m_search->setFocus();
}

int QuickSearch::currentId() const
{
    QModelIndex index = m_list->currentIndex();
    return index.isValid() ? m_model->data(index, SnippetModel::IdRole).toInt() : -1;
}

void QuickSearch::copyCurrent()
{
    int id = currentId();
    if (id == -1) return;
    Snippet snippet = Database::instance().snippet(id);
    // Copy while the window still has the focus (Wayland requires it)
    Clipboard::copySnippet(snippet);
    hide();
    emit copied(snippet.title);
}

bool QuickSearch::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            // Move through the results while typing
            QApplication::sendEvent(m_list, event);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (keyEvent->modifiers() & Qt::ControlModifier) {
                int id = currentId();
                if (id != -1) {
                    hide();
                    emit openRequested(id);
                }
            } else {
                copyCurrent();
            }
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool QuickSearch::event(QEvent *event)
{
    // Close when the user clicks elsewhere, like a menu
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        hide();
    }
    return QWidget::event(event);
}
