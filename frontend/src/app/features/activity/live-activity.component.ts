import { Component, computed, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { JsonViewerComponent } from '../../shared/components/json-viewer/json-viewer.component';
import { EmptyStateComponent } from '../../shared/components/empty-state/empty-state.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { KNOWN_EVENT_TYPES } from '../../core/models';
import { EventStreamService } from '../../core/services/event-stream.service';

/** Raw view of GET /api/v1/stream (Phase 21.6) — every envelope the backend
 * has published since this page was opened, filterable by event type. */
@Component({
  selector: 'app-live-activity',
  standalone: true,
  imports: [RouterLink, PageHeaderComponent, StatusBadgeComponent, JsonViewerComponent, EmptyStateComponent, IconComponent, RelativeTimePipe],
  templateUrl: './live-activity.component.html',
})
export class LiveActivityComponent {
  readonly stream = inject(EventStreamService);
  readonly knownTypes = KNOWN_EVENT_TYPES;
  readonly activeFilters = signal<Set<string>>(new Set());

  readonly filtered = computed(() => {
    const filters = this.activeFilters();
    const items = this.stream.activity();
    return filters.size === 0 ? items : items.filter((i) => filters.has(i.type));
  });

  toggleFilter(type: string): void {
    const next = new Set(this.activeFilters());
    if (next.has(type)) next.delete(type);
    else next.add(type);
    this.activeFilters.set(next);
  }

  clearFilters(): void {
    this.activeFilters.set(new Set());
  }

  clearActivity(): void {
    this.stream.clear();
  }
}
