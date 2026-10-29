import { Component, OnInit, inject, signal } from '@angular/core';
import { Router, RouterLink } from '@angular/router';
import { DataTableColumn, DataTableComponent } from '../../shared/components/data-table/data-table.component';
import { CellTemplateDirective } from '../../shared/components/data-table/cell-template.directive';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { DevicePickerComponent } from '../../shared/components/device-picker/device-picker.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { IncidentRecord } from '../../core/models';
import { IncidentService } from '../../core/services/incident.service';

@Component({
  selector: 'app-incident-list',
  standalone: true,
  imports: [
    RouterLink,
    DataTableComponent,
    CellTemplateDirective,
    PageHeaderComponent,
    StatusBadgeComponent,
    DevicePickerComponent,
    IconComponent,
    RelativeTimePipe,
  ],
  templateUrl: './incident-list.component.html',
})
export class IncidentListComponent implements OnInit {
  private readonly incidentService = inject(IncidentService);
  private readonly router = inject(Router);

  readonly loading = signal(true);
  readonly rows = signal<IncidentRecord[]>([]);
  readonly selectedDevice = signal<string | null>(null);

  readonly columns: DataTableColumn<IncidentRecord>[] = [
    { key: 'incidentId', header: 'Incident ID' },
    { key: 'deviceId', header: 'Device' },
    { key: 'state', header: 'State' },
    { key: 'firstReceivedAt', header: 'First Seen' },
    { key: 'lastReceivedAt', header: 'Last Update' },
  ];

  ngOnInit(): void {
    this.load();
  }

  onDeviceChange(deviceId: string | null): void {
    this.selectedDevice.set(deviceId);
    this.load();
  }

  load(): void {
    this.loading.set(true);
    this.incidentService.list(this.selectedDevice() ?? undefined, 500).subscribe({
      next: (rows) => {
        this.rows.set(rows);
        this.loading.set(false);
      },
      error: () => this.loading.set(false),
    });
  }

  open(incidentId: string): void {
    this.router.navigate(['/incidents', incidentId]);
  }

  readonly rowKeyFn = (r: IncidentRecord) => r.incidentId;
}
