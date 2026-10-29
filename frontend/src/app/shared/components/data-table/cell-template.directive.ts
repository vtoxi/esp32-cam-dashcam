import { Directive, Input, TemplateRef } from '@angular/core';

/**
 * Lets a page override how one column's cells render while everything else
 * (search, sort, pagination) stays generic in DataTableComponent:
 *   <ng-template appCellTemplate="status" let-row>...</ng-template>
 */
@Directive({
  selector: 'ng-template[appCellTemplate]',
  standalone: true,
})
export class CellTemplateDirective {
  @Input('appCellTemplate') columnKey = '';
  constructor(public readonly template: TemplateRef<{ $implicit: unknown }>) {}
}
