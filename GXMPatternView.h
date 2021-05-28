#ifndef LIBGXMPATTERNVIEW_H_INCLUDED
#define LIBGXMPATTERNVIEW_H_INCLUDED

namespace GXMPatternView
{
    void InitViewer(int HeadSize, bool PatternDisplay = false, bool DetailedView = true);
    void SetDetailed(bool True = true);
    void MoveNext(bool True = true);
    char *DrawPatternView();
}

#endif
