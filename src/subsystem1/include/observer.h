#ifndef OBSERVER_H
#define OBSERVER_H


class Observer {
public:
    virtual ~Observer() {}
    
    virtual void update(int case_id, int evidence_id) = 0;
};





#endif 
