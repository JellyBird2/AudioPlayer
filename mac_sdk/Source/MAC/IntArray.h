#pragma once

class CIntArray : public CArray<int, int &>
{
public:
    CIntArray();
    virtual ~CIntArray() APE_OVERRIDE;

    void SortAscending();
    void SortDescending();

protected:
    static int SortAscendingCallback(const void * pNumberA, const void * pNumberB);
    static int SortDescendingCallback(const void * pNumberA, const void * pNumberB);
};
