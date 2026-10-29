import { NgTemplateOutlet } from '@angular/common';
import { Component, ContentChildren, Input, QueryList, computed, signal } from '@angular/core';
import { IconComponent } from '../icon/icon.component';
import { EmptyStateComponent } from '../empty-state/empty-state.component';
import { CellTemplateDirective } from './cell-template.directive';

export interface DataTableColumn<T> {
  key: string;
  header: string;
  /** Falls back to `row[key]` when omitted. Also drives search matching and sort ordering. */
  value?: (row: T) => string | number | null | undefined;
  sortable?: boolean;
  numeric?: boolean;
  widthClass?: string;
}

type SortDirection = 'asc' | 'desc' | null;

/**
 * The one table implementation every feature page (devices, telemetry,
 * events, incidents, commands, webhooks) is built on — client-side search,
 * sort, and pagination implemented once here, generic over row type `T`.
 * Custom cell rendering (badges, links, buttons) is opted into per column via
 * <ng-template appCellTemplate="key" let-row>; everything else is automatic.
 * No Angular Material/CDK — a hand-rolled signal-driven table styled with
 * Tailwind, matching the rest of the app.
 */
@Component({
  selector: 'app-data-table',
  standalone: true,
  imports: [IconComponent, EmptyStateComponent, NgTemplateOutlet],
  templateUrl: './data-table.component.html',
})
export class DataTableComponent<T extends object> {
  @Input({ required: true }) columns: DataTableColumn<T>[] = [];
  @Input() set rows(value: T[] | null) {
    this.rowsSignal.set(value ?? []);
    this.page.set(0);
  }
  @Input() loading = false;
  @Input() searchPlaceholder = 'Search…';
  @Input() pageSizeOptions = [10, 25, 50, 100];
  @Input() set defaultPageSize(value: number) {
    this.pageSize.set(value);
  }
  @Input() emptyMessage = 'No records found.';
  @Input() emptyIcon = 'inbox';
  @Input() emptyHint = '';
  @Input() rowKey: (row: T) => string | number = (row, i = 0) => JSON.stringify(row) + i;

  @ContentChildren(CellTemplateDirective) cellTemplates!: QueryList<CellTemplateDirective>;

  private readonly rowsSignal = signal<T[]>([]);
  readonly search = signal('');
  readonly sortKey = signal<string | null>(null);
  readonly sortDirection = signal<SortDirection>(null);
  readonly page = signal(0);
  readonly pageSize = signal(25);

  readonly filtered = computed(() => {
    const term = this.search().trim().toLowerCase();
    if (!term) return this.rowsSignal();
    return this.rowsSignal().filter((row) =>
      this.columns.some((col) => {
        const value = col.value ? col.value(row) : (row as Record<string, unknown>)[col.key];
        return value !== null && value !== undefined && String(value).toLowerCase().includes(term);
      }),
    );
  });

  readonly sorted = computed(() => {
    const key = this.sortKey();
    const dir = this.sortDirection();
    const rows = [...this.filtered()];
    if (!key || !dir) return rows;
    const col = this.columns.find((c) => c.key === key);
    rows.sort((a, b) => {
      const av = (col?.value ? col.value(a) : (a as Record<string, unknown>)[key]) ?? '';
      const bv = (col?.value ? col.value(b) : (b as Record<string, unknown>)[key]) ?? '';
      const cmp = typeof av === 'number' && typeof bv === 'number' ? av - bv : String(av).localeCompare(String(bv));
      return dir === 'asc' ? cmp : -cmp;
    });
    return rows;
  });

  readonly totalPages = computed(() => Math.max(1, Math.ceil(this.sorted().length / this.pageSize())));

  readonly paged = computed(() => {
    const size = this.pageSize();
    const start = this.page() * size;
    return this.sorted().slice(start, start + size);
  });

  readonly rangeStart = computed(() => (this.sorted().length === 0 ? 0 : this.page() * this.pageSize() + 1));
  readonly rangeEnd = computed(() => Math.min(this.sorted().length, (this.page() + 1) * this.pageSize()));

  onSearch(value: string): void {
    this.search.set(value);
    this.page.set(0);
  }

  toggleSort(col: DataTableColumn<T>): void {
    if (col.sortable === false) return;
    if (this.sortKey() !== col.key) {
      this.sortKey.set(col.key);
      this.sortDirection.set('asc');
      return;
    }
    const current = this.sortDirection();
    this.sortDirection.set(current === 'asc' ? 'desc' : current === 'desc' ? null : 'asc');
    if (this.sortDirection() === null) this.sortKey.set(null);
  }

  sortIconFor(col: DataTableColumn<T>): string {
    if (this.sortKey() !== col.key) return 'chevron-up-down';
    return this.sortDirection() === 'asc' ? 'chevron-up' : 'chevron-down';
  }

  changePageSize(size: number): void {
    this.pageSize.set(size);
    this.page.set(0);
  }

  goToPage(index: number): void {
    this.page.set(Math.max(0, Math.min(index, this.totalPages() - 1)));
  }

  cellValue(row: T, col: DataTableColumn<T>): unknown {
    return col.value ? col.value(row) : (row as Record<string, unknown>)[col.key];
  }

  templateFor(key: string) {
    return this.cellTemplates?.find((t) => t.columnKey === key)?.template;
  }
}
