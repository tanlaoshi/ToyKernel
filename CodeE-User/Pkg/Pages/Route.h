#ifndef PAGE_ROUTE_H
#define PAGE_ROUTE_H

void RouteInit(int Wid);
void RouteSetPage(int Wid, int Page);
int RoutePage(void);
/* Poll 事件 → 导航优先，否则当前页 Dispatch。关窗返回 1。 */
int RouteDispatch(int Wid, int Ev);

#endif
