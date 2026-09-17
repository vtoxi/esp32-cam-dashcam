import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatCardComponent } from '../../shared/components/stat-card/stat-card.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { EmptyStateComponent } from '../../shared/components/empty-state/empty-state.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { HealthSummary } from '../../core/models';
import { HealthService } from '../../core/services/health.service';
import { EventStreamService } from '../../core/services/event-stream.service';

@Component({
  selector: 'app-dashboard',
  standalone: true,
  imports: [RouterLink, PageHeaderComponent, StatCardComponent, StatusBadgeComponent, EmptyStateComponent, IconComponent, RelativeTimePipe],
  templateUrl: './dashboard.component.html',
})
export class DashboardComponent implements OnInit {
  private readonly healthService = inject(HealthService);
  readonly stream = inject(EventStreamService);

  readonly loading = signal(true);
  readonly health = signal<HealthSummary | null>(null);
  readonly loadError = signal(false);

  readonly offlineCount = computed(() => {
    const h = this.health();
    return h ? h.deviceCount - h.onlineCount : 0;
  });

  readonly recentActivity = computed(() => this.stream.activity().slice(0, 8));

  readonly quickLinks = [
    { path: '/devices', label: 'Devices', icon: 'device-phone', description: 'Registered gateways and their status' },
    { path: '/incidents', label: 'Incidents', icon: 'exclamation-triangle', description: 'Security incidents and evidence' },
    { path: '/commands', label: 'Commands', icon: 'terminal', description: 'Issue remote commands' },
    { path: '/webhooks', label: 'Webhooks', icon: 'link', description: 'Manage outbound integrations' },
  ];

  ngOnInit(): void {
    this.load();
  }

  load(): void {
    this.loading.set(true);
    this.healthService.get().subscribe({
      next: (h) => {
        this.health.set(h);
        this.loading.set(false);
        this.loadError.set(false);
      },
      error: () => {
        this.loading.set(false);
        this.loadError.set(true);
      },
    });
  }
}
