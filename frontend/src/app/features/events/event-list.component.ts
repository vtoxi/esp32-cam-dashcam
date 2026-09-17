import { Component, OnInit, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { DataTableColumn, DataTableComponent } from '../../shared/components/data-table/data-table.component';
import { CellTemplateDirective } from '../../shared/components/data-table/cell-template.directive';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { JsonViewerComponent } from '../../shared/components/json-viewer/json-viewer.component';
import { DevicePickerComponent } from '../../shared/components/device-picker/device-picker.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { EventRecord } from '../../core/models';
import { EventLogService } from '../../core/services/telemetry.service';

@Component({
  selector: 'app-event-list',
  standalone: true,
  imports: [
    RouterLink,
    DataTableComponent,
    CellTemplateDirective,
    PageHeaderComponent,
    JsonViewerComponent,
    DevicePickerComponent,
    IconComponent,
    RelativeTimePipe,
  ],
  templateUrl: './event-list.component.html',
})
export class EventListComponent implements OnInit {
  private readonly eventService = inject(EventLogService);

  readonly loading = signal(true);
  readonly rows = signal<EventRecord[]>([]);
  readonly selectedDevice = signal<string | null>(null);

  readonly columns: DataTableColumn<EventRecord>[] = [
    { key: 'deviceId', header: 'Device' },
    { key: 'receivedAt', header: 'Received' },
    { key: 'payloadJson', header: 'Payload', sortable: false },
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
    this.eventService.list(this.selectedDevice() ?? undefined, 500).subscribe({
      next: (rows) => {
        this.rows.set(rows);
        this.loading.set(false);
      },
      error: () => this.loading.set(false),
    });
  }

  readonly rowKeyFn = (r: EventRecord) => `${r.deviceId}-${r.id}`;
}
